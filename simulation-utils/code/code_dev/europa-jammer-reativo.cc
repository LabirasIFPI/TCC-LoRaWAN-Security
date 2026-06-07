#include "ns3/core-module.h"
#include "ns3/energy-module.h"
#include "ns3/lorawan-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/okumura-hata-propagation-loss-model.h" // Adicionado
#include <fstream>
#include <set>

using namespace ns3;
using namespace lorawan;
using namespace ns3::energy;

int enviados_eu = 0;
int recebidos_eu = 0;
Ptr<EndDeviceLorawanMac> jammerMacGlobal = nullptr;

void
ContarEnvio(Ptr<const Packet> p)
{
    enviados_eu++;
}

void
ContarRecebido(Ptr<const Packet> p)
{
    recebidos_eu++;
}

// --- LÓGICA DO JAMMER REATIVO ---
void
AtirarRuido()
{
    if (jammerMacGlobal != nullptr)
    {
        Ptr<Packet> lixo = Create<Packet>(50); // Pacote de interferência
        jammerMacGlobal->Send(lixo);
    }
}

void
ReagirAoEnvio(Ptr<const Packet> p)
{
    // CORREÇÃO 1: Atraso reduzido para 10ms para atingir o SF7 no ar
    Simulator::Schedule(MilliSeconds(10), &AtirarRuido);
}

int
main(int argc, char* argv[])
{
    uint32_t sf = 12;
    double dist = 1000.0;
    bool rural = false;
    bool jammer = false;

    CommandLine cmd;
    cmd.AddValue("sf", "SF (7 ou 12)", sf);
    cmd.AddValue("dist", "Distancia em metros", dist);
    cmd.AddValue("rural", "true para Rural, false para Urbano", rural);
    cmd.AddValue("jammer", "true para Jammer ON, false para OFF", jammer);
    cmd.Parse(argc, argv);

    // --- 1. PROPAGAÇÃO (OKUMURA-HATA + SOMBREAMENTO) ---
    // CORREÇÃO 2: Substituindo LogDistance pelo Okumura-Hata
    Ptr<OkumuraHataPropagationLossModel> loss = CreateObject<OkumuraHataPropagationLossModel>();
    loss->SetAttribute("Frequency", DoubleValue(868e6)); // Frequência exata: 868 MHz

    // CORREÇÃO COM OS NOMES EXATOS DA SUA VERSÃO DO NS-3:
    if (rural)
    {
        loss->SetAttribute("Environment", StringValue("OpenAreas"));
    }
    else
    {
        loss->SetAttribute("Environment", StringValue("Urban"));
        loss->SetAttribute("CitySize", StringValue("Large"));
    }

    // Mantemos o Fator Caos (Sombreamento Log-Normal) para simular obstáculos aleatórios
    Ptr<RandomPropagationLossModel> shadowing = CreateObject<RandomPropagationLossModel>();
    Ptr<NormalRandomVariable> rv = CreateObject<NormalRandomVariable>();
    rv->SetAttribute("Mean", DoubleValue(0.0));
    rv->SetAttribute("Variance", DoubleValue(rural ? 16.0 : 64.0));
    shadowing->SetAttribute("Variable", PointerValue(rv));

    loss->SetNext(shadowing); // Encadeia Okumura-Hata com Sombreamento

    Ptr<LoraChannel> channel =
        CreateObject<LoraChannel>(loss, CreateObject<ConstantSpeedPropagationDelayModel>());

    LoraHelper helper;
    LoraPhyHelper phyHelper;
    phyHelper.SetChannel(channel);

    // CORREÇÃO 3: Potência de transmissão legal da Europa (14 dBm)
    phyHelper.Set("TxPower", DoubleValue(14.0));

    LorawanMacHelper macHelper;
    macHelper.SetRegion(LorawanMacHelper::EU);
    macHelper.SetDeviceType(LorawanMacHelper::ED_A);
    macHelper.SetAddressGenerator(CreateObject<LoraDeviceAddressGenerator>());

    // --- 2. NÓS E MOBILIDADE ---
    NodeContainer ed;
    ed.Create(1);
    NodeContainer gw;
    gw.Create(1);

    MobilityHelper mobility;
    Ptr<ListPositionAllocator> alloc = CreateObject<ListPositionAllocator>();
    // CORREÇÃO 4: Altura realista das antenas (Z). Crucial para o Okumura-Hata funcionar!
    alloc->Add(Vector(dist, 0, 1.5)); // Sensor a 1.5 metros do chão
    alloc->Add(Vector(0, 0, 30.0));   // Gateway no alto de uma torre de 30 metros

    mobility.SetPositionAllocator(alloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(ed);
    mobility.Install(gw);

    // --- 3. INSTALAÇÃO DO LORA E ENERGIA ---
    phyHelper.SetDeviceType(LoraPhyHelper::ED);
    NetDeviceContainer edNetDevices = helper.Install(phyHelper, macHelper, ed);

    phyHelper.SetDeviceType(LoraPhyHelper::GW);
    macHelper.SetDeviceType(LorawanMacHelper::GW);
    helper.Install(phyHelper, macHelper, gw);

    BasicEnergySourceHelper energySourceHelper;
    energySourceHelper.Set("BasicEnergySourceInitialEnergyJ", DoubleValue(1000.0));
    EnergySourceContainer sources = energySourceHelper.Install(ed);

    LoraRadioEnergyModelHelper radioEnergyHelper;
    DeviceEnergyModelContainer deviceModels = radioEnergyHelper.Install(edNetDevices, sources);

    Ptr<EndDeviceLorawanMac> mac =
        edNetDevices.Get(0)->GetObject<LoraNetDevice>()->GetMac()->GetObject<EndDeviceLorawanMac>();
    mac->SetDataRate((sf == 7) ? 5 : 0);

    PeriodicSenderHelper ps;
    // CORREÇÃO 5: Intervalo realista para não estourar o Duty Cycle o tempo todo
    ps.SetPeriod(Minutes(10)); // Envia a cada 10 minutos
    ps.Install(ed);

    // --- 4. CONFIGURAÇÃO DO JAMMER ---
    if (jammer)
    {
        NodeContainer jammerNode;
        jammerNode.Create(1);

        Ptr<ListPositionAllocator> allocJam = CreateObject<ListPositionAllocator>();
        allocJam->Add(Vector(50, 0, 1.5)); // Jammer perto do Gateway, no nível do chão
        MobilityHelper mobJam;
        mobJam.SetPositionAllocator(allocJam);
        mobJam.SetMobilityModel("ns3::ConstantPositionMobilityModel");
        mobJam.Install(jammerNode);

        phyHelper.SetDeviceType(LoraPhyHelper::ED);
        macHelper.SetDeviceType(LorawanMacHelper::ED_A);
        NetDeviceContainer jamDevice = helper.Install(phyHelper, macHelper, jammerNode);

        jammerMacGlobal = jamDevice.Get(0)
                              ->GetObject<LoraNetDevice>()
                              ->GetMac()
                              ->GetObject<EndDeviceLorawanMac>();
        jammerMacGlobal->SetDataRate((sf == 7) ? 5 : 0);

        Config::ConnectWithoutContext(
            "/NodeList/0/DeviceList/0/$ns3::LoraNetDevice/Mac/SentNewPacket",
            MakeCallback(&ReagirAoEnvio));
    }

    Config::ConnectWithoutContext("/NodeList/0/DeviceList/0/$ns3::LoraNetDevice/Mac/SentNewPacket",
                                  MakeCallback(&ContarEnvio));
    Config::ConnectWithoutContext("/NodeList/1/DeviceList/0/$ns3::LoraNetDevice/Mac/ReceivedPacket",
                                  MakeCallback(&ContarRecebido));

    Simulator::Stop(Hours(24));

    std::cout << "\nSimulando CENÁRIO EUROPA (Jammer: " << (jammer ? "ON" : "OFF") << " | "
              << (rural ? "Rural" : "Urbano") << " | SF" << sf << ")...\n";

    Simulator::Run();

    // --- 5. RESULTADOS E CSV ---
    double energyGasta = sources.Get(0)->GetInitialEnergy() - sources.Get(0)->GetRemainingEnergy();

    std::ofstream csv;
    csv.open("resultados_europa_reativo.csv", std::ios_base::app);
    csv << sf << "," << (rural ? "Rural" : "Urbano") << "," << dist << "," << enviados_eu << ","
        << recebidos_eu << "," << energyGasta << "," << (jammer ? "Atacado" : "Limpo") << "\n";
    csv.close();

    std::cout << "Finalizado! ENVIADOS: " << enviados_eu << " | RECEBIDOS: " << recebidos_eu
              << " | Energia Gasta: " << energyGasta << " Joules\n";

    Simulator::Destroy();
    return 0;
}