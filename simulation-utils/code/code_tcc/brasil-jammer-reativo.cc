#include "ns3/core-module.h"
#include "ns3/energy-module.h"
#include "ns3/lorawan-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"

#include <fstream>
#include <set>
#include <ctime>

using namespace ns3;
using namespace lorawan;
using namespace ns3::energy;

int enviados_br = 0;
int recebidos_br = 0;
std::set<uint64_t> pacotes_legitimos;

Ptr<EndDeviceLorawanMac> jammerMacGlobal = nullptr;
Ptr<MobilityModel> jammerMobilityGlobal = nullptr;

uint32_t sfGlobal = 7;

// ---------------- JAMMER ----------------
void
AtirarRuido()
{
    if (jammerMacGlobal)
    {
        Ptr<Packet> lixo = Create<Packet>(50);
        jammerMacGlobal->Send(lixo);
    }
}

// Tempo de reação real do hardware SDR — Šabić et al. (2025)
double
ObterTempoDeReacaoSDR(uint32_t sf)
{
    double meanMs = 0.0;
    double stdDevMs = 0.0;

    switch (sf)
    {
    case 7:  meanMs = 14.10;  stdDevMs = 3.01;  break;
    case 8:  meanMs = 22.22;  stdDevMs = 2.27;  break;
    case 9:  meanMs = 26.26;  stdDevMs = 3.29;  break;
    case 10: meanMs = 42.79;  stdDevMs = 9.55;  break;
    case 11: meanMs = 66.32;  stdDevMs = 12.55; break;
    case 12: meanMs = 126.64; stdDevMs = 19.26; break;
    default: meanMs = 14.10;  stdDevMs = 3.01;  break;
    }

    double meanSec     = meanMs / 1000.0;
    double stdDevSec   = stdDevMs / 1000.0;
    double varianceSec = stdDevSec * stdDevSec;

    Ptr<NormalRandomVariable> randTime = CreateObject<NormalRandomVariable>();
    randTime->SetAttribute("Mean",     DoubleValue(meanSec));
    randTime->SetAttribute("Variance", DoubleValue(varianceSec));

    double delay = randTime->GetValue();
    if (delay < 0.005)
        delay = 0.005;

    return delay;
}

void
ContarEnvio(std::string context, Ptr<const Packet> p)
{
    enviados_br++;
    pacotes_legitimos.insert(p->GetUid());

    if (jammerMacGlobal && jammerMobilityGlobal)
    {
        size_t firstSlash  = context.find("/", 1);
        size_t secondSlash = context.find("/", firstSlash + 1);
        uint32_t senderId  = std::stoi(context.substr(firstSlash + 1, secondSlash - firstSlash - 1));

        Ptr<Node>          senderNode     = NodeList::GetNode(senderId);
        Ptr<MobilityModel> senderMobility = senderNode->GetObject<MobilityModel>();

        double distancia          = senderMobility->GetDistanceFrom(jammerMobilityGlobal);
        double raioDeEscutaDoJammer = 2500.0;

        if (distancia <= raioDeEscutaDoJammer)
        {
            double reactionTime = ObterTempoDeReacaoSDR(sfGlobal);
            Simulator::Schedule(Seconds(reactionTime), &AtirarRuido);
        }
    }
}

void
ContarRecebido(Ptr<const Packet> p)
{
    if (pacotes_legitimos.count(p->GetUid()))
        recebidos_br++;
}

int
main(int argc, char* argv[])
{
    double   dist     = 5000;
    bool     rural    = true;
    bool     jammer   = false;
    uint32_t numNodes = 30;

    uint32_t seed = 1; 
    CommandLine cmd;
    cmd.AddValue("sf",      "Spreading Factor",          sfGlobal);
    cmd.AddValue("dist",    "Distancia",                 dist);
    cmd.AddValue("rural",   "Ambiente",                  rural);
    cmd.AddValue("jammer",  "Atacante",                  jammer);
    cmd.AddValue("nNodes",  "Numero de nos",             numNodes);
    cmd.AddValue("seed",   "Semente RNG",       seed); 
    cmd.Parse(argc, argv);

    RngSeedManager::SetSeed(seed);
    RngSeedManager::SetRun(1);

    // ---------------- CANAL ----------------
    Ptr<LogDistancePropagationLossModel> loss = CreateObject<LogDistancePropagationLossModel>();
    loss->SetPathLossExponent(rural ? 2.8 : 3.5);

    Ptr<LoraChannel> channel =
        CreateObject<LoraChannel>(loss, CreateObject<ConstantSpeedPropagationDelayModel>());

    // ---------------- HELPERS ----------------
    LoraHelper     helper;
    LoraPhyHelper  phyHelper;
    phyHelper.SetChannel(channel);

    LorawanMacHelper macHelper;
    // CORREÇÃO 1: EU é a única região completamente implementada no ns-3.45.
    // Canais sobrescritos manualmente para 915 MHz (Anatel / AU915).
    macHelper.SetRegion(LorawanMacHelper::EU);

    // ---------------- NÓS ----------------
    NodeContainer ed;       ed.Create(numNodes);
    NodeContainer gw;       gw.Create(1);
    NodeContainer jammerNode;
    if (jammer) jammerNode.Create(1);

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
    if (jammer) jammerDevs = helper.Install(phyHelper, macHelper, jammerNode);

    phyHelper.SetDeviceType(LoraPhyHelper::GW);
    macHelper.SetDeviceType(LorawanMacHelper::GW);
    NetDeviceContainer gwDevs = helper.Install(phyHelper, macHelper, gw);

    // Matriz que trava o SF em todos os Data Rates
    std::vector<uint8_t> sfMatrix(8, (uint8_t)sfGlobal);

    // DR correspondente ao SF (SF7=DR5, SF8=DR4, ... SF12=DR0)
    uint8_t dr = (sfGlobal >= 7 && sfGlobal <= 12) ? (12 - sfGlobal) : 5;

    // ---------------- CONFIG EDs ----------------
    for (uint32_t i = 0; i < edDevs.GetN(); i++)
    {
        Ptr<LoraNetDevice>       dev = edDevs.Get(i)->GetObject<LoraNetDevice>();
        if (!dev) continue;
        Ptr<EndDeviceLorawanMac> mac = dev->GetMac()->GetObject<EndDeviceLorawanMac>();
        if (!mac) continue;

        mac->SetSfForDataRate(sfMatrix);
        // CORREÇÃO 2: setar o DR explicitamente para travar o SF de verdade
        mac->SetDataRate(dr);
        mac->SetMType(LorawanMacHeader::CONFIRMED_DATA_UP);

        // CORREÇÃO 3: canais na faixa brasileira 915 MHz
        Ptr<LogicalLoraChannelHelper> chHelper = Create<LogicalLoraChannelHelper>(3);
        Ptr<SubBand> subBand = Create<SubBand>(915000000, 928000000, 1.0, 14);
        chHelper->AddSubBand(subBand);

        double freqs[3] = {915200000, 915400000, 915600000};
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
        Ptr<LoraNetDevice>       dev = jammerDevs.Get(0)->GetObject<LoraNetDevice>();
        if (dev)
        {
            Ptr<EndDeviceLorawanMac> mac = dev->GetMac()->GetObject<EndDeviceLorawanMac>();
            if (mac)
            {
                jammerMacGlobal = mac;
                mac->SetSfForDataRate(sfMatrix);
                mac->SetDataRate(5); // Jammer sempre em SF7 — máxima taxa de interferência

                Ptr<LogicalLoraChannelHelper> chHelper = Create<LogicalLoraChannelHelper>(3);
                Ptr<SubBand> subBand = Create<SubBand>(915000000, 928000000, 1.0, 14);
                chHelper->AddSubBand(subBand);

                double freqs[3] = {915200000, 915400000, 915600000};
                for (uint8_t i = 0; i < 3; i++)
                {
                    Ptr<LogicalLoraChannel> ch = Create<LogicalLoraChannel>(freqs[i], 0, 5);
                    chHelper->SetChannel(i, ch);
                }
                mac->SetLogicalLoraChannelHelper(chHelper);
            }
        }
    }

    // CORREÇÃO 4: Gateway não precisa de AddFrequency manual —
    // o helper EU já configurou as frequências corretas (915 MHz após edição do helper)

    // ---------------- NETWORK SERVER ----------------
    NodeContainer nsNode; nsNode.Create(1);

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("10Gbps"));
    p2p.SetChannelAttribute("Delay",   StringValue("2ms"));
    NetDeviceContainer p2pNetDevs = p2p.Install(nsNode.Get(0), gw.Get(0));

    // CORREÇÃO 5: registrar gateway corretamente no Network Server
    P2PGwRegistration_t gwRegistration;
    gwRegistration.emplace_back(p2pNetDevs.Get(0)->GetObject<PointToPointNetDevice>(), gw.Get(0));

    NetworkServerHelper nsHelper;
    nsHelper.SetEndDevices(ed);
    nsHelper.SetGatewaysP2P(gwRegistration);
    nsHelper.EnableAdr(false); // ADR desligado — SF fixo é o objetivo deste código
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

    if (gw.GetN() > 0)
    {
        std::ostringstream gwPath;
        gwPath << "/NodeList/" << gw.Get(0)->GetId()
               << "/DeviceList/0/$ns3::LoraNetDevice/Mac/ReceivedPacket";
        Config::ConnectWithoutContext(gwPath.str(), MakeCallback(&ContarRecebido));
    }

    Simulator::Stop(Hours(24));

    std::cout << "\nSimulando Brasil | SF" << sfGlobal << " | DR" << (int)dr
              << " | " << (rural ? "Rural" : "Urbano")
              << " | Jammer: " << (jammer ? "ON" : "OFF") << std::endl;

    Simulator::Run();

    double total = 0;
    for (uint32_t i = 0; i < sources.GetN(); i++)
        total += (sources.Get(i)->GetInitialEnergy() - sources.Get(i)->GetRemainingEnergy());
    double media = total / numNodes;

    std::ofstream csv;
    csv.open("resultados_brasil_sf_fixo.csv", std::ios_base::app);
    csv << sfGlobal << "," << (rural ? "Rural" : "Urbano") << "," << dist << "," << numNodes
        << "," << enviados_br << "," << recebidos_br << "," << media << ","
        << (jammer ? "Atacado" : "Limpo") << "," << seed << "\n";
    csv.close();

    std::cout << "ENVIADOS: " << enviados_br << " | RECEBIDOS: " << recebidos_br
              << " | Energia: " << media << " J\n";

    Simulator::Destroy();
    return 0;
}