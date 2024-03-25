#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/ipv4-global-routing-helper.h"

#include "ns3/ptr.h"
#include "ns3/address.h"
#include "ns3/event-id.h"
#include "ns3/data-rate.h"
#include "ns3/application.h"
#include "ns3/traced-callback.h"
#include "ns3/seq-ts-size-header.h"

#include <vector>
#include <map>
#include <utility>
#include <set>

#include "my-source.h"
#include <string>
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <iomanip>

#define LINK_CAPACITY_BASE    1000000000          // 1Gbps
#define BUFFER_SIZE 250                           // 250 packets

using namespace ns3;

enum RunMode {
    TLB, // 
    CONGA, // 3
    CONGA_FLOW,
    CONGA_ECMP,
    PRESTO, // 5
    WEIGHTED_PRESTO, // Distribute the packet according to the topology // 6
    DRB, // 4
    FlowBender, // 7
    ECMP, 
    Clove, // 8
    DRILL, // 9
    LetFlow // 10
};

enum AQM {
  TCN,
  ECNSharp
};

void
ParseAppBw (std::string input, std::vector<std::string>* values)
{
  size_t startPos = 0;
  size_t commaPos;

  // Search for commas and split the string within the loop
  while ((commaPos = input.find(',', startPos)) != std::string::npos) {
    std::string item = input.substr(startPos, commaPos - startPos);
    (*values).push_back(item);

    startPos = commaPos + 1;
  }

  // Process the last substring
  std::string lastItem = input.substr(startPos);
  (*values).push_back(lastItem);
}

double app_seconds_start0 = 0.1;

void
ParseAppSecondsChange (std::string input, std::vector<double>* values)
{
  size_t startPos = 0;
  size_t commaPos;

  // added on 09.01 to specify different start time 
  int i = 0;

  // added on 09.02 to calculate delay in latter applications
  double first_time = 0;

  // Search for commas and split the string within the loop
  while ((commaPos = input.find(',', startPos)) != std::string::npos) {

    std::string item = input.substr(startPos, commaPos - startPos);
    double value = std::stod(item);
    if (i != 0) {
      (*values).push_back(value - first_time);
    }
    else {
      first_time = value;
      app_seconds_start0 = value;   
      }
    startPos = commaPos + 1;

    i++;
  }
  
  // Process the last substring
  std::string lastItem = input.substr(startPos);
  double lastValue = std::stod(lastItem);
  (*values).push_back(lastValue - first_time);
}

std::string result_dir = "tmp_index";

static void
RxWithAddressesPacketSink (Ptr<const Packet> p, const Address &) {
  MySourceIDTag tag;
  if (p->FindFirstMatchingByteTag(tag)) {}

  std::ofstream throput_ofs2 (result_dir + "/received_ms.dat", std::ios::out | std::ios::app);
  throput_ofs2 << "[" << Simulator::Now ().GetMilliSeconds() << "] SourceIDTag: " << tag.Get() << ", size: " << p->GetSize()
              << std::endl;
}

// create an example to check where the packet drops
Ptr<DelayQueueDisc> exampleDQD;
Ptr<QueueDisc> exampleQD;

void
PrintProgress (Time interval)
{
  std::size_t n1 = exampleDQD -> GetNInternalQueues();
  std::size_t n2 = exampleQD -> GetNInternalQueues();
  NS_LOG_INFO ("Progress: " << std::fixed << std::setprecision (1) << Simulator::Now ().GetSeconds () << "[s]");
  NS_LOG_INFO ("Number of internal queues in exampleDQD: " << n1);
  NS_LOG_INFO ("Number of internal queues in exampleQD: " << n2);
  Simulator::Schedule (interval, &PrintProgress, interval);
}

int main (int argc, char *argv[])
{
#if 1
  LogComponentEnable ("LargeScale", LOG_LEVEL_INFO);
#endif

  // Command line parameters parsing
  unsigned randomSeed = 0;
  std::string transportProt = "DcTcp";

  std::string aqmStr = "ECNSharp";

  // The simulation starting and ending time
  double START_TIME = 0.0;
  double END_TIME = 0.5;

  uint32_t linkLatency = 10;

  int SERVER_COUNT = 6;
  int SPINE_COUNT = 2;
  int LEAF_COUNT = 2;
  int LINK_COUNT = 2;

  uint64_t spineLeafCapacity = 10;
  uint64_t leafServerCapacity = 10;
  // serve as bandwidth

  uint32_t TCNThreshold = 80;

  uint32_t ECNSharpInterval = 150;
  uint32_t ECNSharpTarget = 10;
  uint32_t ECNSharpMarkingThreshold = 80;

  CommandLine cmd;
  cmd.AddValue ("StartTime", "Start time of the simulation", START_TIME);
  cmd.AddValue ("EndTime", "End time of the simulation", END_TIME);
  cmd.AddValue ("randomSeed", "Random seed, 0 for random generated", randomSeed);
  cmd.AddValue ("transportProt", "Transport protocol to use: Tcp, DcTcp", transportProt);
  cmd.AddValue ("linkLatency", "Link latency, should be in MicroSeconds", linkLatency);

  cmd.AddValue ("serverCount", "The Server count", SERVER_COUNT);
  cmd.AddValue ("spineCount", "The Spine count", SPINE_COUNT);
  cmd.AddValue ("leafCount", "The Leaf count", LEAF_COUNT);
  cmd.AddValue ("linkCount", "The Link count", LINK_COUNT);

  cmd.AddValue ("spineLeafCapacity", "Spine <-> Leaf capacity in Gbps", spineLeafCapacity);
  cmd.AddValue ("leafServerCapacity", "Leaf <-> Server capacity in Gbps", leafServerCapacity);

  cmd.AddValue ("AQM", "AQM to use: TCN or ECNSharp", aqmStr);

  cmd.AddValue ("TCNThreshold", "The threshold for TCN", TCNThreshold);

  cmd.AddValue ("ECNShaprInterval", "The persistent interval for ECNSharp", ECNSharpInterval);
  cmd.AddValue ("ECNSharpTarget", "The persistent target for ECNShapr", ECNSharpTarget);
  cmd.AddValue ("ECNShaprMarkingThreshold", "The instantaneous marking threshold for ECNSharp", ECNSharpMarkingThreshold);

  cmd.Parse (argc, argv);

  uint64_t SPINE_LEAF_CAPACITY = spineLeafCapacity * LINK_CAPACITY_BASE;
  uint64_t LEAF_SERVER_CAPACITY = leafServerCapacity * LINK_CAPACITY_BASE;
  Time LINK_LATENCY = MicroSeconds (linkLatency);

  AQM aqm;
  if (aqmStr.compare ("TCN") == 0)
    {
      aqm = TCN;
    }
  else if (aqmStr.compare ("ECNSharp") == 0)
    {
      aqm = ECNSharp;
    }
  else
    {
      return 0;
    }

  // Create the result directory
  std::string flag = "initial";
  result_dir += "/" + flag + "_" + aqmStr;
  if (access(result_dir.c_str(), F_OK) == -1 ) {
    std::string create_dir_cmd = "mkdir -p " + result_dir;
    if (system (create_dir_cmd.c_str ()) == -1) {
      std::cout << "ERR: " << create_dir_cmd << " failed, proceed anyway." << std::endl;
    }
  }
  else {
    std::string rm_dir_cmd1 = "rm -rf " + result_dir + "/received_ms.dat";
    if (system (rm_dir_cmd1.c_str ()) == -1) {
      std::cout << "ERR: " << rm_dir_cmd1 << " failed, proceed anyway." << std::endl;
    };
    std::string rm_dir_cmd2 = "rm -rf " + result_dir + "/sent_ms.dat";
    if (system (rm_dir_cmd2.c_str ()) == -1) {
      std::cout << "ERR: " << rm_dir_cmd2 << " failed, proceed anyway." << std::endl;
    };
  }

  if (transportProt.compare ("DcTcp") == 0)
    {
      NS_LOG_INFO ("Enabling DcTcp");
      Config::SetDefault ("ns3::TcpL4Protocol::SocketType", TypeIdValue (TcpDCTCP::GetTypeId ()));

      // TCN Configuration
      Config::SetDefault ("ns3::TCNQueueDisc::Mode", StringValue ("QUEUE_MODE_PACKETS"));
      Config::SetDefault ("ns3::TCNQueueDisc::MaxPackets", UintegerValue (BUFFER_SIZE));
      Config::SetDefault ("ns3::TCNQueueDisc::Threshold", TimeValue (MicroSeconds (TCNThreshold)));

      // ECN Sharp Configuration
      Config::SetDefault ("ns3::ECNSharpQueueDisc::Mode", StringValue ("QUEUE_MODE_PACKETS"));
      Config::SetDefault ("ns3::ECNSharpQueueDisc::MaxPackets", UintegerValue (BUFFER_SIZE));
      Config::SetDefault ("ns3::ECNSharpQueueDisc::InstantaneousMarkingThreshold", TimeValue (MicroSeconds (ECNSharpMarkingThreshold)));
      Config::SetDefault ("ns3::ECNSharpQueueDisc::PersistentMarkingTarget", TimeValue (MicroSeconds (ECNSharpTarget)));
      Config::SetDefault ("ns3::ECNSharpQueueDisc::PersistentMarkingInterval", TimeValue (MicroSeconds (ECNSharpInterval)));
    }

  NS_LOG_INFO ("Config parameters");
  Config::SetDefault ("ns3::TcpSocket::DelAckCount", UintegerValue (0));
  Config::SetDefault ("ns3::TcpSocket::ConnTimeout", TimeValue (MilliSeconds (5)));

  Config::SetDefault ("ns3::TcpSocket::InitialCwnd", UintegerValue (10));
  Config::SetDefault ("ns3::TcpSocketBase::MinRto", TimeValue (MilliSeconds (5)));
  Config::SetDefault ("ns3::TcpSocketBase::ClockGranularity", TimeValue (MicroSeconds (100)));
  Config::SetDefault ("ns3::RttEstimator::InitialEstimation", TimeValue (MicroSeconds (80)));
  Config::SetDefault ("ns3::TcpSocket::SndBufSize", UintegerValue (160000000));
  Config::SetDefault ("ns3::TcpSocket::RcvBufSize", UintegerValue (160000000));

  Config::SetDefault ("ns3::Ipv4GlobalRouting::PerflowEcmpRouting", BooleanValue(true));

  NodeContainer spines;
  spines.Create (SPINE_COUNT);
  NodeContainer leaves;
  leaves.Create (LEAF_COUNT);
  NodeContainer servers;
  servers.Create (SERVER_COUNT * LEAF_COUNT);

  NS_LOG_INFO ("Install Internet stacks");
  InternetStackHelper internet;
  Ipv4GlobalRoutingHelper globalRoutingHelper;

  internet.SetRoutingHelper (globalRoutingHelper);

  internet.Install (servers);
  internet.Install (spines);
  internet.Install (leaves);

  NS_LOG_INFO ("Install channels and assign addresses");

  PointToPointHelper p2p;
  Ipv4AddressHelper ipv4;

  NS_LOG_INFO ("Configuring servers");
  // Setting servers
  p2p.SetDeviceAttribute ("DataRate", DataRateValue (DataRate (LEAF_SERVER_CAPACITY)));
  p2p.SetChannelAttribute ("Delay", TimeValue(LINK_LATENCY));
  p2p.SetQueue ("ns3::DropTailQueue", "MaxPackets", UintegerValue (10));

  ipv4.SetBase ("10.1.0.0", "255.255.255.0");

  Ipv4InterfaceContainer serverInterfaceContainer;

  for (int i = 0; i < LEAF_COUNT; i++)
    {
      ipv4.NewNetwork ();

      for (int j = 0; j < SERVER_COUNT; j++)
        {
          int serverIndex = i * SERVER_COUNT + j;
          NodeContainer nodeContainer = NodeContainer (servers.Get (serverIndex), leaves.Get (i));
          NetDeviceContainer netDeviceContainer = p2p.Install (nodeContainer);

          //TODO We should change this, at endhost we are not going to mark ECN but add delay using delay queue disc

          Ptr<DelayQueueDisc> delayQueueDisc = CreateObject<DelayQueueDisc> ();
          Ptr<Ipv4SimplePacketFilter> filter = CreateObject<Ipv4SimplePacketFilter> ();

          delayQueueDisc->AddPacketFilter (filter);

          delayQueueDisc->AddDelayClass (0, MicroSeconds (1));
          delayQueueDisc->AddDelayClass (1, MicroSeconds (20));
          delayQueueDisc->AddDelayClass (2, MicroSeconds (50));
          delayQueueDisc->AddDelayClass (3, MicroSeconds (80));
          delayQueueDisc->AddDelayClass (4, MicroSeconds (160));

          ObjectFactory switchSideQueueFactory;

          if (aqm == TCN)
            {
              switchSideQueueFactory.SetTypeId ("ns3::TCNQueueDisc");
            }
          else
            {
              switchSideQueueFactory.SetTypeId ("ns3::ECNSharpQueueDisc");
            }
          Ptr<QueueDisc> switchSideQueueDisc = switchSideQueueFactory.Create<QueueDisc> ();

          // configure the example to be the first server - leaf
          if (i == 0 && j == 0) {
            exampleDQD = delayQueueDisc;
            exampleQD = delayQueueDisc;
          }

          Ptr<NetDevice> netDevice0 = netDeviceContainer.Get (0);
          Ptr<TrafficControlLayer> tcl0 = netDevice0->GetNode ()->GetObject<TrafficControlLayer> ();

          delayQueueDisc->SetNetDevice (netDevice0);
          tcl0->SetRootQueueDiscOnDevice (netDevice0, delayQueueDisc);

          Ptr<NetDevice> netDevice1 = netDeviceContainer.Get (1);
          Ptr<TrafficControlLayer> tcl1 = netDevice1->GetNode ()->GetObject<TrafficControlLayer> ();
          switchSideQueueDisc->SetNetDevice (netDevice1);
          tcl1->SetRootQueueDiscOnDevice (netDevice1, switchSideQueueDisc);
          Ipv4InterfaceContainer ifc = ipv4.Assign (netDeviceContainer); 
          Ipv4InterfaceContainer interfaceContainer;
          interfaceContainer.Add(ifc);
          serverInterfaceContainer.Add(ifc.Get(0));

          NS_LOG_INFO ("Leaf - " << i << " is connected to Server - " << j << " with address "
                       << interfaceContainer.GetAddress(1) << " <-> " << interfaceContainer.GetAddress (0)
                       << " with port " << netDeviceContainer.Get (1)->GetIfIndex () << " <-> " << netDeviceContainer.Get (0)->GetIfIndex ());
        }
    }

  NS_LOG_INFO ("Configuring switches");
  // Setting up switches
  p2p.SetDeviceAttribute ("DataRate", DataRateValue (DataRate (SPINE_LEAF_CAPACITY)));

  for (int i = 0; i < LEAF_COUNT; i++)
    {
      for (int j = 0; j < SPINE_COUNT; j++)
        {

          for (int l = 0; l < LINK_COUNT; l++)
            {
              ipv4.NewNetwork ();

              NodeContainer nodeContainer = NodeContainer (leaves.Get (i), spines.Get (j));
              NetDeviceContainer netDeviceContainer = p2p.Install (nodeContainer);
              ObjectFactory switchSideQueueFactory;

              if (aqm == TCN)
                {
                  switchSideQueueFactory.SetTypeId ("ns3::TCNQueueDisc");
                }
              else
                {
                  switchSideQueueFactory.SetTypeId ("ns3::ECNSharpQueueDisc");
                }

              Ptr<QueueDisc> leafQueueDisc = switchSideQueueFactory.Create<QueueDisc> ();

              Ptr<NetDevice> netDevice0 = netDeviceContainer.Get (0);
              Ptr<TrafficControlLayer> tcl0 = netDevice0->GetNode ()->GetObject<TrafficControlLayer> ();
              leafQueueDisc->SetNetDevice (netDevice0);
              tcl0->SetRootQueueDiscOnDevice (netDevice0, leafQueueDisc);

              Ptr<QueueDisc> spineQueueDisc = switchSideQueueFactory.Create<QueueDisc> ();

              Ptr<NetDevice> netDevice1 = netDeviceContainer.Get (1);
              Ptr<TrafficControlLayer> tcl1 = netDevice1->GetNode ()->GetObject<TrafficControlLayer> ();
              spineQueueDisc->SetNetDevice (netDevice1);
              tcl1->SetRootQueueDiscOnDevice (netDevice1, spineQueueDisc);

              Ipv4InterfaceContainer ipv4InterfaceContainer = ipv4.Assign (netDeviceContainer);
              NS_LOG_INFO ("Leaf - " << i << " is connected to Spine - " << j << " with address "
                           << ipv4InterfaceContainer.GetAddress(0) << " <-> " << ipv4InterfaceContainer.GetAddress (1)
                           << " with port " << netDeviceContainer.Get (0)->GetIfIndex () << " <-> " << netDeviceContainer.Get (1)->GetIfIndex ()
                           << " with data rate " << spineLeafCapacity);
            }
        }
    }

  NS_LOG_INFO ("Populate global routing tables");
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  double oversubRatio = static_cast<double>(SERVER_COUNT * LEAF_SERVER_CAPACITY) / (SPINE_LEAF_CAPACITY * SPINE_COUNT * LINK_COUNT);
  NS_LOG_INFO ("Over-subscription ratio: " << oversubRatio);

  NS_LOG_INFO ("Initialize random seed: " << randomSeed);
  if (randomSeed == 0)
    {
      srand ((unsigned)time (NULL));
    }
  else
    {
      srand (randomSeed);
    }

  NS_LOG_INFO("================== Generate application ==================");

  Ptr<MySource>* sources;
  sources = new Ptr<MySource>[LEAF_COUNT * SERVER_COUNT / 2];

  uint32_t app_packet_size = 1440; // in bytes
  std::vector<std::string> app_bw0{};
  std::string app_bw0_str = "10Mbps";
  ParseAppBw(app_bw0_str, &app_bw0);
  std::vector<double> app_seconds_change0{};
  std::string app_sc0_str = "0,10";
  ParseAppSecondsChange(app_sc0_str, &app_seconds_change0);

  for (int i = 0; i < LEAF_COUNT * SERVER_COUNT / 2; ++i) {
    uint16_t sinkPort = 8080;
    
    /////////////////////////////////////////////////////////////////////////////   This is the receive application (rightleaf part)   /////////////////////////////////////////////////////////////////////////////////////////////
    int END_TIME2 = 5;
    Ptr<PacketSink> sink = CreateObject<PacketSink> ();
    sink->SetAttribute ("Protocol", StringValue ("ns3::TcpSocketFactory"));
    sink->SetAttribute ("Local", AddressValue (InetSocketAddress (Ipv4Address::GetAny (), sinkPort)));
    servers.Get(i + LEAF_COUNT * SERVER_COUNT / 2)->AddApplication(sink);
    sink->SetStartTime (Seconds (START_TIME));
    sink->SetStopTime (Seconds (END_TIME2));  
    sink->TraceConnectWithoutContext("Rx", MakeCallback(&RxWithAddressesPacketSink));
    //////////////////////////////////////////////////////////////////////////////////   receive application ends here   ///////////////////////////////////////////////////////////////////////////////////////////////////////////

    /////////////////////////////////////////////////////////////////////////////   This is the send application (leftleaf part)   /////////////////////////////////////////////////////////////////////////////////////////////////
    Address sinkAddress (InetSocketAddress (serverInterfaceContainer.GetAddress(i + LEAF_COUNT * SERVER_COUNT / 2), sinkPort));
    Ptr<Socket> ns3TcpSocket = Socket::CreateSocket (servers.Get (i), TcpSocketFactory::GetTypeId ());
    Ptr<MySource> app = CreateObject<MySource> ();
    app->Setup (ns3TcpSocket, sinkAddress, app_packet_size, &app_bw0, i, false, result_dir, &app_seconds_change0); // i provides the source id for the packets
    app->SetStartTime (Seconds (START_TIME));                             
    servers.Get (i)->AddApplication (app);
    app->SetStopTime (Seconds (END_TIME));
    sources[i] = app;
    //////////////////////////////////////////////////////////////////////////////////   send application ends here   ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
  }

  // Monitor Flow
  Ptr<FlowMonitor> flowMonitor;
  FlowMonitorHelper flowHelper;
  flowMonitor = flowHelper.InstallAll();
  flowMonitor->CheckForLostPackets ();
  std::stringstream flowMonitorFilename;
  flowMonitorFilename << LEAF_COUNT << "X" << SPINE_COUNT << "_" << aqmStr << "_"  << transportProt << ".xml";

  NS_LOG_DEBUG("================== Tracing ==================");
  uint32_t progress_interval_ms = 100;
  Simulator::Schedule (MilliSeconds(progress_interval_ms), &PrintProgress, MilliSeconds(progress_interval_ms));

  NS_LOG_INFO ("Start simulation");
  Simulator::Stop (Seconds (END_TIME));
  Simulator::Run ();

  flowMonitor->SerializeToXmlFile(flowMonitorFilename.str (), true, true);

  Simulator::Destroy ();
  NS_LOG_INFO ("Stop simulation");
}
