#include "MessagingController.h"
#include "IqrfCdcChannel.h"
#include "IqrfSpiChannel.h"
#include "DpaHandler.h"
#include "JsonUtils.h"

#include "IClient.h"
//TODO temporary here
#include "TestClient.h"
#include "ClientService.h"

//TODO temporary here
#include "IMessaging.h"
#include "UdpMessaging.h"
#include "MqMessaging.h"
#include "MqttMessaging.h"
#include "Scheduler.h"

#include "SimpleSerializer.h"
#include "JsonSerializer.h"

#include "IqrfLogging.h"
#include "PlatformDep.h"

const std::string CFG_VERSION("v0.0");

using namespace rapidjson;

void MessagingController::executeDpaTransaction(DpaTransaction& dpaTransaction)
{
  m_dpaTransactionQueue->pushToQueue(&dpaTransaction);
  //TODO wait here for something
}

//called from task queue thread passed by lambda in task queue ctor
void MessagingController::executeDpaTransactionFunc(DpaTransaction* dpaTransaction)
{
  if (m_dpaHandler) {
    try {
      m_dpaHandler->ExecuteDpaTransaction(*dpaTransaction);
    }
    catch (std::exception& e) {
      CATCH_EX("Error in ExecuteDpaTransaction: ", std::exception, e);
      dpaTransaction->processFinish(DpaRequest::kCreated);
    }
  }
  else {
    TRC_ERR("Dpa interface is not working");
    dpaTransaction->processFinish(DpaRequest::kCreated);
  }
}

void MessagingController::registerAsyncDpaMessageHandler(std::function<void(const DpaMessage&)> asyncHandler)
{
  m_asyncHandler = asyncHandler;
  m_dpaHandler->RegisterAsyncMessageHandler([&](const DpaMessage& dpaMessage) {
    m_asyncHandler(dpaMessage);
  });
}

MessagingController::MessagingController(const std::string& iqrfPortName, const std::string& cfgFileName)
  :m_iqrfInterface(nullptr)
  , m_dpaHandler(nullptr)
  , m_dpaTransactionQueue(nullptr)
  , m_iqrfPortName(iqrfPortName)
  , m_scheduler(nullptr)
{
  //try {
    jutils::parseJsonFile(cfgFileName, m_configuration);
    jutils::assertIsObject("", m_configuration);
    
    // check cfg version
    // TODO major.minor ...
    std::string cfgVersion = jutils::getMemberAs<std::string>("Configuration", m_configuration);
    if (cfgVersion != CFG_VERSION) {
      THROW_EX(std::logic_error, "Unexpected configuration: " << PAR(cfgVersion) << "expected: " << PAR(CFG_VERSION));
    }
  //}
  //catch (std::exception &e) {
  //  m_lastError = e.what();
  //}
}

MessagingController::~MessagingController()
{
}

std::set<IMessaging*>& MessagingController::getSetOfMessaging()
{
  return m_protocols;
}

void MessagingController::registerMessaging(IMessaging& messaging)
{
  m_protocols.insert(&messaging);
}

void MessagingController::unregisterMessaging(IMessaging& messaging)
{
  m_protocols.erase(&messaging);
}

void MessagingController::watchDog()
{
  TRC_ENTER("");
  m_running = true;
  try {
    start();
    //TODO wait on condition until 3000
    while (m_running)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      //TODO
      //watch worker threads and possibly restart
    }
  }
  catch (std::exception& e) {
    CATCH_EX("error", std::exception, e);
  }

  stop();
  TRC_LEAVE("");
}

void MessagingController::startClients()
{
  TRC_ENTER("");


  ///////// Messagings ///////////////////////////////////
  //TODO load Messagings plugins
  MqttMessaging* mqttMessaging(nullptr);
  try {
    mqttMessaging = ant_new MqttMessaging();
    m_messagings.push_back(mqttMessaging);
  }
  catch (std::exception &e) {
    CATCH_EX("Cannot create MqttMessaging ", std::exception, e);
  }

  MqMessaging* mqMessaging(nullptr);
  try {
    mqMessaging = ant_new MqMessaging();
    m_messagings.push_back(mqMessaging);
  }
  catch (std::exception &e) {
    CATCH_EX("Cannot create MqMessaging ", std::exception, e);
  }

  UdpMessaging* udpMessaging(nullptr);
  try {
    udpMessaging = ant_new UdpMessaging();
    udpMessaging->setDaemon(this);
    m_messagings.push_back(udpMessaging);
  }
  catch (std::exception &e) {
    CATCH_EX("Cannot create UdpMessaging ", std::exception, e);
  }

  ///////// Serializers ///////////////////////////////////
  //TODO load Serializers plugins
  DpaTaskSimpleSerializerFactory* simpleSerializer = ant_new DpaTaskSimpleSerializerFactory();
  m_serializers.push_back(simpleSerializer);

  DpaTaskJsonSerializerFactory* jsonSerializer = ant_new DpaTaskJsonSerializerFactory();
  m_serializers.push_back(jsonSerializer);

  //////// Clients //////////////////////////////////
  //TODO load clients plugins
  IClient* client1 = ant_new TestClient("TestClient1");
  client1->setDaemon(this);
  client1->setMessaging(mqttMessaging);
  client1->setSerializer(simpleSerializer);
  m_clients.insert(std::make_pair(client1->getClientName(), client1));

  IClient* client2 = ant_new TestClient("TestClient2");
  client2->setDaemon(this);
  client2->setMessaging(mqttMessaging);
  client2->setSerializer(jsonSerializer);
  m_clients.insert(std::make_pair(client2->getClientName(), client2));

  IClient* clientIqrfapp = ant_new TestClient("TestClientIqrfapp");
  clientIqrfapp->setDaemon(this);
  clientIqrfapp->setMessaging(mqMessaging);
  clientIqrfapp->setSerializer(simpleSerializer);
  m_clients.insert(std::make_pair(clientIqrfapp->getClientName(), clientIqrfapp));

  IClient* clientService = ant_new ClientService("ClientService");
  clientIqrfapp->setDaemon(this);
  clientIqrfapp->setMessaging(mqMessaging);
  clientIqrfapp->setSerializer(simpleSerializer);
  m_clients.insert(std::make_pair(clientIqrfapp->getClientName(), clientIqrfapp));

  /////////////////////
  for (auto cli : m_clients) {
    try {
      cli.second->start();
    }
    catch (std::exception &e) {
      CATCH_EX("Cannot start " << NAME_PAR(client, cli.second->getClientName()), std::exception, e);
    }
  }

  for (auto ms : m_messagings) {
    try {
      ms->start();
    }
    catch (std::exception &e) {
      CATCH_EX("Cannot start messaging ", std::exception, e);
    }
  }

  TRC_LEAVE("");
}

void MessagingController::stopClients()
{
  TRC_ENTER("");
  for (auto cli : m_clients) {
    cli.second->stop();
    delete cli.second;
  }
  m_clients.clear();

  for (auto ms : m_messagings) {
    ms->stop();
    delete ms;
  }
  m_messagings.clear();

  for (auto sr : m_serializers) {
    delete sr;
  }
  m_serializers.clear();

  TRC_LEAVE("");
}

void MessagingController::start()
{
  TRC_ENTER("");

  try {
    size_t found = m_iqrfPortName.find("spi");
    if (found != std::string::npos)
      m_iqrfInterface = ant_new IqrfSpiChannel(m_iqrfPortName);
    else
      m_iqrfInterface = ant_new IqrfCdcChannel(m_iqrfPortName);

    m_dpaHandler = ant_new DpaHandler(m_iqrfInterface);

    m_dpaHandler->Timeout(100);    // Default timeout is infinite
  }
  catch (std::exception& ae) {
    TRC_ERR("There was an error during DPA handler creation: " << ae.what());
  }

  m_dpaTransactionQueue = ant_new TaskQueue<DpaTransaction*>([&](DpaTransaction* trans) {
    executeDpaTransactionFunc(trans);
  });

  m_scheduler = ant_new Scheduler();

  startClients();

  m_scheduler->start();

  TRC_INF("daemon started");
  TRC_LEAVE("");
}

void MessagingController::stop()
{
  TRC_ENTER("");
  TRC_INF("daemon stops");

  m_scheduler->stop();

  stopClients();

  delete m_scheduler;

  //TODO unregister call-backs first ?
  delete m_iqrfInterface;
  delete m_dpaTransactionQueue;
  delete m_dpaHandler;

  TRC_LEAVE("");
}

void MessagingController::exit()
{
  TRC_INF("exiting ...");
  m_running = false;
}
