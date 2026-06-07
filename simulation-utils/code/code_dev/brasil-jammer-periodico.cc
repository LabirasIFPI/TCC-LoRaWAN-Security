#include "ns3/core-module.h"
#include "ns3/energy-module.h"
#include "ns3/lorawan-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include <fstream>
#include <set>

using namespace ns3;
using namespace lorawan;
using namespace ns3::energy;

int enviados_br = 0;
int recebidos_br = 0;
std::set<uint64_t> pacotes_legitimos; // Guarda os UIDs originais

void
ContarEnvio(Ptr<const Packet> p)
{
    enviados_br++;
    // Salva a "impressão digital" do pacote gerado pelo SEU sensor
    pacotes_legitimos.insert(p->GetUid());
}

void
ContarRecebido(Ptr<const Packet> p)
{
    // A Gateway só conta como sucesso se a impressão digital for do seu sensor
    if (pacotes_legitimos.find(p->GetUid()) != pacotes_legitimos.end())
    {
        recebidos_br++;
    }
}

int
main(int argc, char* argv[])
{
    uint32_t sf = 12;
    double dist = 5000.0;
    bool rural = true;
    bool jammer = true; // NOVO: Flag para ligar o ataque

    CommandLine cmd;
    cmd.AddValue("sf", "SF (7 ou 12)", sf);
    cmd.AddValue("dist", "Distancia em metros", dist);
    cmd.AddValue("rural", "true para Rural, false para Urbano", rural);
    cmd.AddValue("jammer", "true para ligar o Atacante", jammer);
    cmd.Parse(argc, argv);

    // --- 1. PROPAGAÇÃO E SHADOWING ---
    Ptr<LogDistancePropagationLossModel> logDistance =
        CreateObject<LogDistancePropagationLossModel>();
    logDistance->SetPathLossExponent(rural ? 2.8 : 3.5);
    logDistance->SetReference(1, 31.5);

    Ptr<RandomPropagationLossModel> fading = CreateObject<RandomPropagationLossModel>();
    Ptr<NormalRandomVariable> rv = CreateObject<NormalRandomVariable>();
    rv->SetAttribute("Mean", DoubleValue(0.0));
    rv->SetAttribute("Variance", DoubleValue(rural ? 16.0 : 64.0));
    fading->SetAttribute("Variable", PointerValue(rv));
    logDistance->SetNext(fading);

    Ptr<LoraChannel> channel =
        CreateObject<LoraChannel>(logDistance, CreateObject<ConstantSpeedPropagationDelayModel>());

    // --- 2. MAC E PHY ---
    LoraHelper helper;
    LoraPhyHelper phyHelper;
    phyHelper.SetChannel(channel);

    LorawanMacHelper macHelper;
    macHelper.SetRegion(LorawanMacHelper::ALOHA);
    macHelper.SetDeviceType(LorawanMacHelper::ED_A);
    macHelper.SetAddressGenerator(CreateObject<LoraDeviceAddressGenerator>());

    // --- CRIANDO OS ATORES ---
    NodeContainer ed;
    ed.Create(1); // Seu Sensor (Nó 0)
    NodeContainer gw;
    gw.Create(1); // Gateway (Nó 1)
    NodeContainer jammerNode;
    if (jammer)
    {
        jammerNode.Create(1); // O Atacante (Nó 2)
    }

    MobilityHelper mobility;
    Ptr<ListPositionAllocator> alloc = CreateObject<ListPositionAllocator>();
    alloc->Add(Vector(dist, 0, 0)); // Sensor na distância D
    alloc->Add(Vector(0, 0, 0));    // Gateway no centro
    if (jammer)
    {
        alloc->Add(Vector(50, 0, 0)); // Jammer colado na Gateway (Cegando a antena)
    }
    mobility.SetPositionAllocator(alloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");

    mobility.Install(ed);
    mobility.Install(gw);
    if (jammer)
    {
        mobility.Install(jammerNode);
    }

    // --- 3. INSTALAÇÃO DOS DISPOSITIVOS ---
    phyHelper.SetDeviceType(LoraPhyHelper::ED);
    NetDeviceContainer edNetDevices = helper.Install(phyHelper, macHelper, ed);

    NetDeviceContainer jammerNetDevices;
    if (jammer)
    {
        jammerNetDevices = helper.Install(phyHelper, macHelper, jammerNode);
    }

    phyHelper.SetDeviceType(LoraPhyHelper::GW);
    macHelper.SetDeviceType(LorawanMacHelper::GW);
    NetDeviceContainer gwNetDevices = helper.Install(phyHelper, macHelper, gw);

    // --- 4. CONFIGURANDO CANAIS (INDEPENDENTES) ---
    Ptr<LogicalLoraChannelHelper> brazilHelper = Create<LogicalLoraChannelHelper>(8);
    Ptr<SubBand> brazilSubBand = Create<SubBand>(915000000, 915200000, 1.0, 14);
    brazilHelper->AddSubBand(brazilSubBand);

    for (uint8_t i = 0; i < 8; i++)
    {
        Ptr<LogicalLoraChannel> ch = Create<LogicalLoraChannel>(915100000, 0, 5);
        brazilHelper->SetChannel(i, ch);
    }

    edNetDevices.Get(0)
        ->GetObject<LoraNetDevice>()
        ->GetMac()
        ->GetObject<EndDeviceLorawanMac>()
        ->SetLogicalLoraChannelHelper(brazilHelper);

    if (jammer)
    {
        // O Jammer ganha um cérebro lógico próprio para não corromper o seu sensor
        Ptr<LogicalLoraChannelHelper> jamHelper = Create<LogicalLoraChannelHelper>(8);
        Ptr<SubBand> jamSubBand = Create<SubBand>(915000000, 915200000, 1.0, 14);
        jamHelper->AddSubBand(jamSubBand);
        for (uint8_t i = 0; i < 8; i++)
        {
            Ptr<LogicalLoraChannel> ch = Create<LogicalLoraChannel>(915100000, 0, 5);
            jamHelper->SetChannel(i, ch);
        }
        jammerNetDevices.Get(0)
            ->GetObject<LoraNetDevice>()
            ->GetMac()
            ->GetObject<EndDeviceLorawanMac>()
            ->SetLogicalLoraChannelHelper(jamHelper);
    }

    // --- 5. CONFIGURAÇÃO DA GATEWAY ---
    gwNetDevices.Get(0)
        ->GetObject<LoraNetDevice>()
        ->GetPhy()
        ->GetObject<GatewayLoraPhy>()
        ->AddFrequency(915100000);

    // --- 6. ENERGIA (Apenas do seu Sensor) ---
    BasicEnergySourceHelper energySourceHelper;
    energySourceHelper.Set("BasicEnergySourceInitialEnergyJ", DoubleValue(10000.0));
    EnergySourceContainer sources = energySourceHelper.Install(ed);
    LoraRadioEnergyModelHelper radioEnergyHelper;
    radioEnergyHelper.Install(edNetDevices, sources);

    // --- 7. MATRIX OVERRIDE (Forçando o SF) ---
    std::vector<uint8_t> sfMatrix = {(uint8_t)sf,
                                     (uint8_t)sf,
                                     (uint8_t)sf,
                                     (uint8_t)sf,
                                     (uint8_t)sf,
                                     (uint8_t)sf,
                                     (uint8_t)sf,
                                     (uint8_t)sf};
    edNetDevices.Get(0)
        ->GetObject<LoraNetDevice>()
        ->GetMac()
        ->GetObject<EndDeviceLorawanMac>()
        ->SetSfForDataRate(sfMatrix);

    if (jammer)
    {
        jammerNetDevices.Get(0)
            ->GetObject<LoraNetDevice>()
            ->GetMac()
            ->GetObject<EndDeviceLorawanMac>()
            ->SetSfForDataRate(sfMatrix);
    }

    // --- 8. APLICAÇÕES ---
    PeriodicSenderHelper ps;
    ps.SetPeriod(Seconds(5));
    ps.Install(ed);

    if (jammer)
    {
        PeriodicSenderHelper jamApp;
        // FÍSICA CORRIGIDA: Esperamos 1.5s para o SF12 (que gasta 1.31s) poder respirar
        jamApp.SetPeriod(Seconds(1.5));
        jamApp.Install(jammerNode);
    }

    // --- 9. RASTREAMENTO DOS PACOTES ---
    // Monitoramos a linha de Envio do NÓ 0 (Seu Sensor)
    Config::ConnectWithoutContext("/NodeList/0/DeviceList/0/$ns3::LoraNetDevice/Mac/SentNewPacket",
                                  MakeCallback(&ContarEnvio));
    // Monitoramos tudo que chega no NÓ 1 (Gateway), e a função filtra pelo UID
    Config::ConnectWithoutContext("/NodeList/1/DeviceList/0/$ns3::LoraNetDevice/Mac/ReceivedPacket",
                                  MakeCallback(&ContarRecebido));

    Simulator::Stop(Hours(24));

    std::cout << "\nSimulando CENÁRIO BRASIL (Jammer: " << (jammer ? "ON" : "OFF") << " | "
              << (rural ? "Rural" : "Urbano") << " | SF" << sf << ")..." << std::endl;
    Simulator::Run();

    // --- 10. RESULTADOS ---
    double energyGasta = sources.Get(0)->GetInitialEnergy() - sources.Get(0)->GetRemainingEnergy();
    std::ofstream csv;
    csv.open("resultados_brasil_periodico.csv", std::ios_base::app);
    csv << sf << "," << (rural ? "Rural" : "Urbano") << "," << dist << "," << enviados_br << ","
        << recebidos_br << "," << energyGasta << "," << (jammer ? "Atacado" : "Limpo") << "\n";
    csv.close();

    std::cout << "Finalizado! ENVIADOS (Legítimos): " << enviados_br
              << " | RECEBIDOS: " << recebidos_br << " | Energia: " << energyGasta << " J"
              << std::endl;

    Simulator::Destroy();
    return 0;
}