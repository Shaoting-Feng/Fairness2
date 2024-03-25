#ifndef MY_SOURCE_H
#define MY_SOURCE_H

#include "ns3/address.h"
#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/internet-module.h"
#include "ns3/data-rate.h"
#include "ns3/traced-callback.h"
#include "ns3/seq-ts-size-header.h"

// added on 07.27
#include <fstream>
#include <string>
#include <iostream>

// added on 08.08 for data rate change
#include <vector>

#include "ns3/log.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("LargeScale");

class MySource : public Application 
{
public:

  MySource ();
  virtual ~MySource();

  // Default OnOffApp creates socket until app start time, can't access and configure the tracing externally
  void Setup (Ptr<Socket> socket, Address address, uint32_t packetSize, std::vector<std::string>* dataRate, uint32_t appid, bool poisson, std::string result_dir, std::vector<double>* change_time);

  uint32_t GetPacketsSent();

private:
  virtual void StartApplication (void);
  virtual void StopApplication (void);

  void ScheduleTx (void);
  void SendPacket (void);

  Ptr<Socket>                     m_socket;
  Address                         m_peer;
  uint32_t                        m_packetSize;
  EventId                         m_sendEvent;
  bool                            m_running;
  uint32_t                        m_packetsSent;
  uint32_t                        m_sourceid;
  Ptr<ExponentialRandomVariable>  m_var;

  // added on 07.27
  std::string                     m_result_dir;
  void StopSendNew (void);

  // added on 08.07 for data rate change
  DataRate                        m_currentDataRate;
  std::vector<std::string>*       m_dataRate;
  std::vector<double>*            m_change_time;
  void ChangeDataRate (uint32_t);
};

MySource::MySource (): 
  m_running (false), 
  m_packetsSent (0)
{
}

MySource::~MySource()
{
  m_socket = 0;
}

void
MySource::Setup (Ptr<Socket> socket, Address address, uint32_t packetSize, std::vector<std::string>* dataRate, uint32_t appid, bool /*unusedParam1*/, std::string result_dir, std::vector<double>* change_time)
{
  m_socket = socket;
  m_peer = address;
  m_packetSize = packetSize;
  m_sourceid = appid;

  m_var = CreateObject<ExponentialRandomVariable> ();
  // No need for poisson as the demand is infinite anyway and the packet gap pattern is determined by the underlying transport

  // added on 07.27
  m_result_dir = result_dir;

  // added on 08.07 for data rate change
  m_dataRate = dataRate;
  m_change_time = change_time;
  m_currentDataRate = DataRate((*m_dataRate)[0]);
}

void
MySource::ChangeDataRate (uint32_t i)
{
  m_currentDataRate = DataRate((*m_dataRate)[i+1]);
  
  // added on 09.01 to handle the delay in changing data rate
  Simulator::Cancel (m_sendEvent);

  if (m_currentDataRate != DataRate("0bps")) {
    ++m_packetsSent;
    ScheduleTx ();
  }
}

void
MySource::StartApplication (void)
{
  m_running = true;
  m_packetsSent = 0;
  m_socket->Bind ();
  m_socket->Connect (m_peer);
  SendPacket ();

  // added on 08.07 for data rate change
  for (uint32_t i = 0; i < (*m_change_time).size()-1; ++i) {
    Simulator::Schedule (Seconds((*m_change_time)[i]), &MySource::ChangeDataRate, this, i);
  }
  Simulator::Schedule (Seconds((*m_change_time)[(*m_change_time).size()-1]), &MySource::StopSendNew, this);

  // NS_LOG_INFO ("Sending starts");
}

void 
MySource::StopSendNew (void)
{
  m_running = false;

  if (m_sendEvent.IsRunning ())
  {
    Simulator::Cancel (m_sendEvent);
  }
}

void 
MySource::StopApplication (void)
{
  if (m_socket)
  {
    m_socket->Close ();
  }
}

void 
MySource::SendPacket (void)
{
  Ptr<Packet> packet = Create<Packet> (m_packetSize);

  MySourceIDTag tag;
  tag.Set (m_sourceid);
  // PacketTag can be pulled out at intermediate NetDevice but not the destination sink app
  // packet->AddPacketTag (tag);
  packet->AddByteTag (tag);  // For persistence

  m_socket->Send (packet);

  // added on 07.27
  std::ofstream throput_ofs2 (m_result_dir + "/sent_ms.dat", std::ios::out | std::ios::app);
  throput_ofs2 << "[" << Simulator::Now ().GetMilliSeconds() << "] SourceIDTag: " << tag.Get() << ", size: " << packet->GetSize()              
              << std::endl;

  // Infinite demand
  // if (++m_packetsSent < m_nPackets)
  ++m_packetsSent;
  ScheduleTx ();
}

void 
MySource::ScheduleTx (void)
{
  if (m_running)
    {
      Time tNext (Seconds (m_packetSize * 8 / static_cast<double> (m_currentDataRate.GetBitRate ())));
      m_sendEvent = Simulator::Schedule (tNext, &MySource::SendPacket, this); // the first parameter is about delay
    }
}

uint32_t MySource::GetPacketsSent (void) {
  return m_packetsSent;
}

#endif