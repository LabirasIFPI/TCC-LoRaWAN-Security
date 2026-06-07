#include "ns3/core-module.h"
#include "ns3/energy-module.h"
#include "ns3/lorawan-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"

#include <fstream>
#include <set>

using namespace ns3;
using namespace lorawan;
using namespace ns3::energy;

int enviados = 0;
int recebidos = 0;
std::set<uint64_t> pacotes_legitimos;

Ptr<EndDeviceLorawanMac> jammerMacGlobal = nullptr;
Ptr<MobilityModel> jammerMobilityGlobal = nullptr;

// ---------------- JAMMER REATIVO ----------------
void
AtirarRuido()
{
    if (jammerMacGlobal)
    {
        Ptr<Packet> lixo = Create<Packet>(100);
        jammerMacGlobal->Send(lixo);
    }
}

void
ContarEnvio(std::string context, Ptr<const Packet> p)
{
    enviados++;
    pacotes_legitimos.insert(p->GetUid());

    if (jammerMacGlobal && jammerMobilityGlobal)
    {
        size_t firstSlash = context.find("/", 1);
        size_t secondSlash = context.find("/", firstSlash + 1);
        uint32_t senderId = std::stoi(context.substr(firstSlash + 1, secondSlash - firstSlash - 1));

        Ptr<Node> senderNode = NodeList::GetNode(senderId);
        Ptr<MobilityModel> senderMobility = senderNode->GetObject<MobilityModel>();

        Ptr<LoraNetDevice> dev = senderNode->GetDevice(0)->GetObject<LoraNetDevice>();
        Ptr<EndDeviceLorawanMac> senderMac = dev->GetMac()->GetObject<EndDeviceLorawanMac>();

        jammerMacGlobal->SetDataRate(5);


        double distancia = senderMobility->GetDistanceFrom(jammerMobilityGlobal);
        double raio_escuta = 3000.0;

        if (distancia <= raio_escuta)
        {
            Simulator::Schedule(MicroSeconds(50), &AtirarRuido);
        }
    }
}

void
ContarRecebido(Ptr<const Packet> p)
{
    if (pacotes_legitimos.count(p->GetUid()))
    {
        recebidos++;
    }
}

int
main(int argc, char* argv[])
{
    uint32_t sf_inicial = 7;
    double dist = 5000;
    bool rural = true;
    bool jammer = false;
    uint32_t numNodes = 10;

    uint32_t seed = 1;
    CommandLine cmd;
    cmd.AddValue("sf", "Spreading Factor Inicial", sf_inicial);
    cmd.AddValue("dist", "Distancia maxima dos nos", dist);
    cmd.AddValue("rural", "Ambiente (true=Rural, false=Urbano)", rural);
    cmd.AddValue("jammer", "Ligar o Atacante", jammer);
    cmd.AddValue("nNodes", "Numero de nos sensores", numNodes);
    cmd.AddValue("seed", "Semente RNG", seed);
    cmd.Parse(argc, argv);

    RngSeedManager::SetSeed(seed);
    RngSeedManager::SetRun(1);

    uint8_t dr_inicial = 5; // SF7 = DR5
    if (sf_inicial >= 7 && sf_inicial <= 12)
    {
        dr_inicial = 12 - sf_inicial;
    }

    // ---------------- CANAL ----------------
    Ptr<LogDistancePropagationLossModel> loss = CreateObject<LogDistancePropagationLossModel>();
    loss->SetPathLossExponent(rural ? 2.8 : 3.5);
    Ptr<LoraChannel> channel =
        CreateObject<LoraChannel>(loss, CreateObject<ConstantSpeedPropagationDelayModel>());

    // ---------------- HELPERS ----------------
    LoraHelper helper;
    LoraPhyHelper phyHelper;
    phyHelper.SetChannel(channel);

    LorawanMacHelper macHelper;
    // EU é a única região completamente implementada no ns-3.45.
    // Os canais são sobrescritos manualmente para 915 MHz (Brasil / Anatel Res. 680/2017).
    macHelper.SetRegion(LorawanMacHelper::EU);
    double freqs[3] = {915200000, 915400000, 915600000};

    // ---------------- NÓS ----------------
    NodeContainer ed;
    ed.Create(numNodes);
    NodeContainer gw;
    gw.Create(1);
    NodeContainer jammerNode;
    if (jammer)
    {
        jammerNode.Create(1);
    }

    // ---------------- MOBILIDADE ----------------
    MobilityHelper mobility;

    Ptr<ListPositionAllocator> gwPos = CreateObject<ListPositionAllocator>();
    gwPos->Add(Vector(0, 0, 0));
    mobility.SetPositionAllocator(gwPos);
    mobility.Install(gw);

    Ptr<RandomDiscPositionAllocator> edPos = CreateObject<RandomDiscPositionAllocator>();
    edPos->SetX(0);
    edPos->SetY(0);
    Ptr<UniformRandomVariable> rho = CreateObject<UniformRandomVariable>();
    rho->SetAttribute("Min", DoubleValue(50));
    rho->SetAttribute("Max", DoubleValue(dist));
    edPos->SetRho(rho);
    mobility.SetPositionAllocator(edPos);
    mobility.Install(ed);

    if (jammer)
    {
        Ptr<ListPositionAllocator> jamPos = CreateObject<ListPositionAllocator>();
        jamPos->Add(Vector(50, 0, 0));
        mobility.SetPositionAllocator(jamPos);
        mobility.Install(jammerNode);
        jammerMobilityGlobal = jammerNode.Get(0)->GetObject<MobilityModel>();
    }

    // ---------------- DISPOSITIVOS ----------------
    phyHelper.SetDeviceType(LoraPhyHelper::ED);
    macHelper.SetDeviceType(LorawanMacHelper::ED_A);
    NetDeviceContainer edDevs = helper.Install(phyHelper, macHelper, ed);

    NetDeviceContainer jammerDevs;
    if (jammer)
    {
        jammerDevs = helper.Install(phyHelper, macHelper, jammerNode);
    }

    phyHelper.SetDeviceType(LoraPhyHelper::GW);
    macHelper.SetDeviceType(LorawanMacHelper::GW);
    NetDeviceContainer gwDevs = helper.Install(phyHelper, macHelper, gw);

    // ---------------- CONFIG EDs ----------------
    for (uint32_t i = 0; i < edDevs.GetN(); i++)
    {
        Ptr<LoraNetDevice> dev = edDevs.Get(i)->GetObject<LoraNetDevice>();
        Ptr<EndDeviceLorawanMac> mac = dev->GetMac()->GetObject<EndDeviceLorawanMac>();

        mac->SetMType(LorawanMacHeader::CONFIRMED_DATA_UP);
        mac->SetDataRate(dr_inicial);

        // Sobrescreve canais para a faixa brasileira 915 MHz
        Ptr<LogicalLoraChannelHelper> chHelper = Create<LogicalLoraChannelHelper>(3);
        Ptr<SubBand> subBand = Create<SubBand>(915000000, 928000000, 1.0, 14);
        chHelper->AddSubBand(subBand);

        for (uint8_t j = 0; j < 3; j++)
        {
            Ptr<LogicalLoraChannel> ch = Create<LogicalLoraChannel>(freqs[j], 0, 5);
            chHelper->SetChannel(j, ch);
        }
        mac->SetLogicalLoraChannelHelper(chHelper);
    }

    // ---------------- CONFIG JAMMER ----------------
    if (jammer && jammerDevs.GetN() > 0)
    {
        Ptr<LoraNetDevice> dev = jammerDevs.Get(0)->GetObject<LoraNetDevice>();
        Ptr<EndDeviceLorawanMac> mac = dev->GetMac()->GetObject<EndDeviceLorawanMac>();
        jammerMacGlobal = mac;

        Ptr<LogicalLoraChannelHelper> chHelper = Create<LogicalLoraChannelHelper>(3);
        Ptr<SubBand> subBand = Create<SubBand>(915000000, 928000000, 1.0, 14);
        chHelper->AddSubBand(subBand);

        for (uint8_t j = 0; j < 3; j++)
        {
            Ptr<LogicalLoraChannel> ch = Create<LogicalLoraChannel>(freqs[j], 0, 5);
            chHelper->SetChannel(j, ch);
        }
        mac->SetLogicalLoraChannelHelper(chHelper);
    }

    // ---------------- NETWORK SERVER ----------------
    NodeContainer nsNode;
    nsNode.Create(1);

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("10Gbps"));
    p2p.SetChannelAttribute("Delay", StringValue("2ms"));
    NetDeviceContainer p2pDevs = p2p.Install(nsNode.Get(0), gw.Get(0));

    P2PGwRegistration_t gwRegistration;
    gwRegistration.emplace_back(p2pDevs.Get(0)->GetObject<PointToPointNetDevice>(), gw.Get(0));

    NetworkServerHelper nsHelper;
    nsHelper.SetEndDevices(ed);
    nsHelper.SetGatewaysP2P(gwRegistration);
    nsHelper.EnableAdr(true);
    nsHelper.Install(nsNode.Get(0));

    ForwarderHelper forwarderHelper;
    forwarderHelper.Install(gw);

    // ---------------- ENERGIA ----------------
    BasicEnergySourceHelper energy;
    energy.Set("BasicEnergySourceInitialEnergyJ", DoubleValue(10000));
    EnergySourceContainer sources = energy.Install(ed);

    LoraRadioEnergyModelHelper radio;
    radio.Install(edDevs, sources);

    // ---------------- TRÁFEGO ----------------
    PeriodicSenderHelper app;
    app.SetPeriod(Minutes(3));

    Ptr<UniformRandomVariable> rvDelay = CreateObject<UniformRandomVariable>();
    rvDelay->SetAttribute("Min", DoubleValue(0.0));
    rvDelay->SetAttribute("Max", DoubleValue(180.0));

    for (uint32_t i = 0; i < ed.GetN(); i++)
    {
        ApplicationContainer a = app.Install(ed.Get(i));
        a.Start(Seconds(rvDelay->GetValue()));
    }

    // ---------------- TRACES ----------------
    for (uint32_t i = 0; i < ed.GetN(); i++)
    {
        std::ostringstream path;
        path << "/NodeList/" << ed.Get(i)->GetId()
             << "/DeviceList/0/$ns3::LoraNetDevice/Mac/SentNewPacket";
        Config::Connect(path.str(), MakeCallback(&ContarEnvio));
    }

    std::ostringstream gwPath;
    gwPath << "/NodeList/" << gw.Get(0)->GetId()
           << "/DeviceList/0/$ns3::LoraNetDevice/Mac/ReceivedPacket";
    Config::ConnectWithoutContext(gwPath.str(), MakeCallback(&ContarRecebido));

    // ---------------- EXECUÇÃO ----------------
    Simulator::Stop(Hours(24));

    std::cout << "\n=== SIMULACAO LORAWAN BRASIL (915 MHz / ADR) ===" << std::endl;
    std::cout << "SF Inicial: " << sf_inicial << " | DR Inicial: " << (int)dr_inicial
              << " | Ambiente: " << (rural ? "Rural" : "Urbano") << std::endl;
    std::cout << "Ataque Reativo: " << (jammer ? "LIGADO" : "DESLIGADO") << std::endl;

    Simulator::Run();

    std::cout << "\n--- DR Final dos Nos (primeiros 5) ---" << std::endl;
    for (uint32_t i = 0; i < std::min((uint32_t)5, edDevs.GetN()); i++)
    {
        Ptr<LoraNetDevice> dev = edDevs.Get(i)->GetObject<LoraNetDevice>();
        Ptr<EndDeviceLorawanMac> mac = dev->GetMac()->GetObject<EndDeviceLorawanMac>();
        std::cout << "No " << i << " terminou com DR: " << (int)mac->GetDataRate() << std::endl;
    }

    double total = 0;
    for (uint32_t i = 0; i < sources.GetN(); i++)
    {
        total += (sources.Get(i)->GetInitialEnergy() - sources.Get(i)->GetRemainingEnergy());
    }
    double media = total / numNodes;

    std::ofstream csv;
    csv.open("resultados_brasil_adr.csv", std::ios_base::app);
    csv << sf_inicial << "," << (rural ? "Rural" : "Urbano") << "," << dist << "," << numNodes
        << "," << enviados << "," << recebidos << "," << media << ","
        << (jammer ? "Atacado" : "Limpo") << "," << seed << "\n";
    csv.close();

    std::cout << "\n--- Resultados Globais ---" << std::endl;
    std::cout << "Enviados: " << enviados << " | Recebidos: " << recebidos << std::endl;
    std::cout << "Energia Media Consumida: " << media << " Joules" << std::endl;

    Simulator::Destroy();
    return 0;
}