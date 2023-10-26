#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

typedef enum Command {
  Login,
  Reboot,
  FactoryReset,
  SetSip,
  GetSip,
  SetNetwork,
  GetNetwork,
  Maintenance,
  Telemetry,
  Call,
  EndCall,
  Gpio,
  AudioTest,
  SpeakerVolume,
  SetAudioVolume,
  GetAudioVolume,
  Ping,
  Update,
  Version,
  Custom,
} Command;

typedef enum RequestType {
  GET,
  POST,
  Unknown,
} RequestType;

typedef struct ClientHTTP ClientHTTP;

typedef struct Interface Interface;

typedef struct Req Req;

typedef struct Urls Urls;

typedef struct Volume {
  int8_t speaker;
  int8_t audio;
} Volume;

typedef struct SipConfig {
  char *sip;
  char *addr;
  uint16_t port;
  char *call;
} SipConfig;

typedef struct NetworkConfig {
  uint32_t addr;
  uint32_t mask;
  uint32_t gate;
  uint8_t eco;
} NetworkConfig;

typedef struct LoginFields {
  char *user;
  char *pass;
} LoginFields;

typedef union DataValue {
  char **string;
  struct Volume **volume;
  struct SipConfig **sip;
  struct NetworkConfig **net;
  struct LoginFields **login;
} DataValue;

typedef void (*CallbackCommand)(enum Command, const union DataValue*, const char*);

typedef struct CResponse {
  const uint8_t *inner;
  uintptr_t size;
} CResponse;

typedef struct CRequest {
  const struct Req *request;
} CRequest;

extern const char *INDEX;

extern const char *STYLE;

extern const char *MAIN;

extern const char *WASM_SCRIPT;

extern const char *WASM;

extern uintptr_t INDEX_SIZE;

extern uintptr_t STYLE_SIZE;

extern uintptr_t MAIN_SIZE;

extern uintptr_t WASM_SCRIPT_SIZE;

extern uintptr_t WASM_SIZE;

struct Interface *create_interface(uint16_t port, uint8_t ws);

void set_interface_callback(struct Interface *interface, CallbackCommand callback);

void run_interface(struct Interface *interface);

void web_server_default(struct Urls *urls, void (*func)(void));

struct Urls *create_url(void);

void add_url(struct Urls *urls, struct CResponse (*func)(const struct CRequest*));

enum RequestType type_request(const struct CRequest *this_);

const char *url(const struct CRequest *this_);

const char *get_query(const struct CRequest *this_, char *ptr);

const char *get_body(const struct CRequest *this_, char *ptr);

struct ClientHTTP *start_client(struct Urls *urls, uint16_t port, uintptr_t thread);

void drop_client(struct ClientHTTP *obj);

void drop_interface(struct Interface *obj);

uint8_t check_pass(char *pass, char *hashed_pass);
