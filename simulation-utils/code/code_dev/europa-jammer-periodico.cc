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

int enviados_eu = 0;
int recebidos_eu = 0;

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

int
main(int argc, char* argv[])
{
    uint32_t sf = 12;
    double dist = 5000.0;
    bool rural = true;

    CommandLine cmd;
    cmd.AddValue("sf", "SF (7 ou 12)", sf);
    cmd.AddValue("dist", "Distancia em metros", dist);
    cmd.AddValue("rural", "true para Rural, false para Urbano", rural);
    cmd.Parse(argc, argv);

    // --- 1. PROPAGAÇÃO ---
    Ptr<LogDistancePropagationLossModel> loss = CreateObject<LogDistancePropagationLossModel>();
    loss->SetPathLossExponent(rural ? 2.1 : 3.5);
    loss->SetReference(1, 31.0);
    Ptr<LoraChannel> channel =
        CreateObject<LoraChannel>(loss, CreateObject<ConstantSpeedPropagationDelayModel>());

    LoraHelper helper;
    LoraPhyHelper phyHelper;
    phyHelper.SetChannel(channel);

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
    alloc->Add(Vector(dist, 0, 0));
    alloc->Add(Vector(0, 0, 0));
    mobility.SetPositionAllocator(alloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(ed);
    mobility.Install(gw);

    // --- 3. INSTALAÇÃO DO LORA (PRECISA VIR ANTES DA ENERGIA) ---
    phyHelper.SetDeviceType(LoraPhyHelper::ED);
    NetDeviceContainer edNetDevices = helper.Install(phyHelper, macHelper, ed);

    phyHelper.SetDeviceType(LoraPhyHelper::GW);
    macHelper.SetDeviceType(LorawanMacHelper::GW);
    helper.Install(phyHelper, macHelper, gw);

    // --- 4. ENERGIA (INSTALADA NOS DISPOSITIVOS) ---
    BasicEnergySourceHelper energySourceHelper;
    energySourceHelper.Set("BasicEnergySourceInitialEnergyJ", DoubleValue(1000.0));
    EnergySourceContainer sources = energySourceHelper.Install(ed);

    LoraRadioEnergyModelHelper radioEnergyHelper;
    // O SEGREDO: Instalamos no NetDeviceContainer, não no NodeContainer
    DeviceEnergyModelContainer deviceModels = radioEnergyHelper.Install(edNetDevices, sources);

    // --- 5. CONFIGURAÇÃO MAC ---
    Ptr<EndDeviceLorawanMac> mac =
        edNetDevices.Get(0)->GetObject<LoraNetDevice>()->GetMac()->GetObject<EndDeviceLorawanMac>();
    mac->SetDataRate((sf == 7) ? 5 : 0);

    PeriodicSenderHelper ps;
    ps.SetPeriod(Seconds(5));
    ps.Install(ed);

    Config::ConnectWithoutContext("/NodeList/0/DeviceList/0/$ns3::LoraNetDevice/Mac/SentNewPacket",
                                  MakeCallback(&ContarEnvio));
    Config::ConnectWithoutContext("/NodeList/1/DeviceList/0/$ns3::LoraNetDevice/Mac/ReceivedPacket",
                                  MakeCallback(&ContarRecebido));

    Simulator::Stop(Hours(24));
    Simulator::Run();

    // --- 6. RESULTADOS E CSV ---
    double energyGasta = sources.Get(0)->GetInitialEnergy() - sources.Get(0)->GetRemainingEnergy();

    std::ofstream csv;
    csv.open("resultados_europa_periodico.csv", std::ios_base::app);
    csv << sf << "," << (rural ? "Rural" : "Urbano") << "," << dist << "," << enviados_eu << ","
        << recebidos_eu << "," << energyGasta << "\n";
    csv.close();

    std::cout << "\nSimulação Europa Finalizada. Dados salvos em 'resultados_europa.csv'"
              << std::endl;
    std::cout << "Energia gasta: " << energyGasta << " Joules" << std::endl;

    Simulator::Destroy();
    return 0;
}