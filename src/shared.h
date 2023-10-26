extern int my_global;
extern bool call_started;
extern int count_s;
extern int REQUEST_CALL;
extern int REGISTERED_BIT;
extern int UNREGISTERED_BIT;
extern int INIT_CALL;

extern TaskHandle_t xHandleSleep;
extern EventGroupHandle_t ethernet_event_group;

void resume();