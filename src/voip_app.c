#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "audio_mem.h"
#include "soc/soc_caps.h"
#include "esp_peripherals.h"
#include "input_key_service.h"
#include "wifi_service.h"
#include "smart_config.h"
#include "sip_service.h"
#include "driver/rtc_io.h"
#include "audio_sys.h"
#include "algorithm_stream.h"
#include "audio_idf_version.h"
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "esp_eth.h"
#include "esp_netif.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "light_sleep_example.h"
#include "shared.h"
#include "sos-server.h"
#include <sys/random.h>
#include <sys/param.h>
#include <assert.h>
#include <errno.h>
#include "esp_random.h"

// Função para obter dados aleatórios
ssize_t getrandom(void *buf, size_t buflen, unsigned int flags) {
    if (buf == NULL) {
        errno = EFAULT; // Define errno para indicar erro de falta de memória
        return -1; // Retorna -1 para indicar erro
    }

    esp_fill_random(buf, buflen); // Preenche o buffer com dados aleatórios usando a função esp_fill_random

    return buflen; // Retorna o tamanho do buffer
}

// Função de preparação para manipulação de forks em threads
int pthread_atfork(void *prepare, void *parent, void *child) {
    return 0; // Retorna 0 para indicar sucesso na operação
}

// Inclusão de cabeçalho condicional com base na versão do ESP-IDF
#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0))
#include "esp_netif.h"
#else
#include "tcpip_adapter.h"
#endif

// Definição de uma constante de string TAG
static const char *TAG = "VoIP_EXAMPLE";

// Declaração de variáveis globais
char sip[30] = "100:100";
char addr[15] = "192.168.0.5";
uint16_t port = 5060;
char call_sip[20] = "800";

// Inicialização de uma estrutura ip_info
esp_netif_ip_info_t ip_info = {
    { 192 | 168 << 8 | 137 << 16 | 50 << 24 },
    { 255 | 255 << 8 | 255 << 16 | 0 << 24 },
    { 192 | 168 << 8 | 137 << 16 | 1 << 24 }
};

// Definição de um tipo enum chamado EconomicMode
typedef enum EconomicMode {
    Disabled,
    Enabled,
    Ultra
} EconomicMode;

// Inicialização de uma variável do tipo EconomicMode
EconomicMode eco_mode = Disabled;

// Declaração de variáveis globais adicionais
nvs_handle_t nvs_storage;
int my_global;
esp_eth_handle_t eth_control;
esp_eth_config_t *eth_config;
EventGroupHandle_t ethernet_event_group;
uint8_t player_volume = 80;
bool started = false;
bool call_started = false;
int count_s = 0;
int CONNECTED_BIT = 0x01;
int REGISTERED_BIT = 0x02;
int REQUEST_CALL = 0x04;
int INIT_CALL = 0x08;
int DISCONNECTED_BIT = 0x10;
int UNREGISTERED_BIT = 0x20;

// Declaração de variáveis estáticas
static bool is_smart_config;
static av_stream_handle_t av_stream;

// Declaração de uma variável de tipo TaskHandle_t
TaskHandle_t xHandleSleep;

// Definição de uma função de comparação e troca atômica
unsigned int __sync_val_compare_and_swap_8(unsigned int *ptr, unsigned int oldval, unsigned int newval) {
  int fail = 1;
  unsigned int actual_oldval;

  while (1) {
    actual_oldval = (volatile unsigned int *)ptr;

    if (__builtin_expect(oldval != actual_oldval, 0))
      return actual_oldval;

    fail = __sync_bool_compare_and_swap(ptr, oldval, newval);

    if (__builtin_expect(!fail, 1))
      return actual_oldval;
  }
}

// Configuração do módulo Ethernet (condicional)
#if CONFIG_EXAMPLE_USE_SPI_ETHERNET
#define INIT_SPI_ETH_MODULE_CONFIG(eth_module_config, num) \
    do { \
        eth_module_config[num].spi_cs_gpio = CONFIG_EXAMPLE_ETH_SPI_CS ##num## _GPIO; \
        eth_module_config[num].int_gpio = CONFIG_EXAMPLE_ETH_SPI_INT ##num## _GPIO; \
        eth_module_config[num].phy_reset_gpio = CONFIG_EXAMPLE_ETH_SPI_PHY_RST ##num## _GPIO; \
        eth_module_config[num].phy_addr = CONFIG_EXAMPLE_ETH_SPI_PHY_ADDR ##num; \
    } while(0)

// Definição de uma estrutura para configuração do módulo Ethernet
typedef struct {
    uint8_t spi_cs_gpio;
    uint8_t int_gpio;
    int8_t phy_reset_gpio;
    uint8_t phy_addr;
} spi_eth_module_config_t;
#endif

// Manipulador de eventos Ethernet
static void eth_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    uint8_t mac_addr[6] = {0};
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        xEventGroupSetBits(ethernet_event_group, CONNECTED_BIT);
        xEventGroupClearBits(ethernet_event_group, DISCONNECTED_BIT);

        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        ESP_LOGI(TAG, "Ethernet Link Up");
        ESP_LOGI(TAG, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
                 mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);

        ESP_LOGI(TAG, "PERIPH_WIFI_CONNECTED [%d]", __LINE__);
        is_smart_config = false;

        vTaskDelay(2000 / portTICK_PERIOD_MS);
        break;

    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "Ethernet Link Down");
        xEventGroupSetBits(ethernet_event_group, DISCONNECTED_BIT);
        xEventGroupClearBits(ethernet_event_group, CONNECTED_BIT | REGISTERED_BIT);
        //vTaskSuspend(xHandleSleep);
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Ethernet Started");
        //vTaskSuspend(xHandleSleep);
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "Ethernet Stopped");
        // xEventGroupClearBits(ethernet_event_group, CONNECTED_BIT | REGISTERED_BIT);
        //vTaskSuspend(xHandleSleep);
        break;
    default:
        break;
    }
}

// Manipulador de eventos IP_EVENT_ETH_GOT_IP
static void got_ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    ESP_LOGI(TAG, "Ethernet Got IP Address");
    ESP_LOGI(TAG, "~~~~~~~~~~~");
    ESP_LOGI(TAG, "ETHIP:" IPSTR, IP2STR(&ip_info->ip));
    ESP_LOGI(TAG, "ETHMASK:" IPSTR, IP2STR(&ip_info->netmask));
    ESP_LOGI(TAG, "ETHGW:" IPSTR, IP2STR(&ip_info->gw));
    ESP_LOGI(TAG, "~~~~~~~~~~~");

    ESP_LOGI(TAG, "[ 5 ] Create SIP Service");

    char *s = malloc(100);
    sprintf(s, "udp://%s@%s:%d", sip, addr, port);

    ESP_LOGI(TAG, "Connect to %s", s);

    esp_sip = sip_service_start(av_stream, s);
}

// Inicializa a rede
void init_network() {
    // Cria instância(s) de esp-netif para módulo(s) SPI Ethernet
    esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_ETH();
    esp_netif_config_t cfg_spi = {
        .base = &esp_netif_config,
        .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH
    };
    esp_netif_t *eth_netif_spi[CONFIG_EXAMPLE_SPI_ETHERNETS_NUM] = { NULL };
    char if_key_str[10];
    char if_desc_str[10];
    char num_str[3];
    for (int i = 0; i < CONFIG_EXAMPLE_SPI_ETHERNETS_NUM; i++) {
        itoa(i, num_str, 10);
        strcat(strcpy(if_key_str, "ETH_SPI_"), num_str);
        strcat(strcpy(if_desc_str, "eth"), num_str);
        esp_netif_config.if_key = if_key_str;
        esp_netif_config.if_desc = if_desc_str;
        esp_netif_config.route_prio = 30 - i;
        eth_netif_spi[i] = esp_netif_new(&cfg_spi);
    }

    // Inicializa configurações MAC e PHY como padrão
    eth_mac_config_t mac_config_spi = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config_spi = ETH_PHY_DEFAULT_CONFIG();

    // Instala manipulador de ISR GPIO para atender interrupções dos módulos SPI Ethernet
    gpio_install_isr_service(0);

    // Inicializa barramento SPI
    spi_bus_config_t buscfg = {
        .miso_io_num = CONFIG_EXAMPLE_ETH_SPI_MISO_GPIO,
        .mosi_io_num = CONFIG_EXAMPLE_ETH_SPI_MOSI_GPIO,
        .sclk_io_num = CONFIG_EXAMPLE_ETH_SPI_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(CONFIG_EXAMPLE_ETH_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // Inicializa configuração específica do módulo SPI Ethernet a partir do Kconfig (CS GPIO, Interrupt GPIO, etc.)
    spi_eth_module_config_t spi_eth_module_config[CONFIG_EXAMPLE_SPI_ETHERNETS_NUM];
    INIT_SPI_ETH_MODULE_CONFIG(spi_eth_module_config, 0);
    
    // Configura a interface SPI e o driver Ethernet para o módulo SPI específico
    esp_eth_mac_t *mac_spi[CONFIG_EXAMPLE_SPI_ETHERNETS_NUM];
    esp_eth_phy_t *phy_spi[CONFIG_EXAMPLE_SPI_ETHERNETS_NUM];
    eth_control = NULL;
    spi_device_interface_config_t spi_devcfg = {
        .mode = 0,
        .clock_speed_hz = CONFIG_EXAMPLE_ETH_SPI_CLOCK_MHZ * 1000 * 1000,
        .queue_size = 20
    };
    for (int i = 0; i < CONFIG_EXAMPLE_SPI_ETHERNETS_NUM; i++) {
        // Define o Chip Select GPIO do módulo SPI
        spi_devcfg.spics_io_num = spi_eth_module_config[i].spi_cs_gpio;
        // Define números GPIO restantes e configuração usada pelo módulo SPI
        phy_config_spi.phy_addr = spi_eth_module_config[i].phy_addr;
        phy_config_spi.reset_gpio_num = spi_eth_module_config[i].phy_reset_gpio;
        
        eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(&spi_devcfg);
        w5500_config.int_gpio_num = spi_eth_module_config[i].int_gpio;
        mac_spi[i] = esp_eth_mac_new_w5500(&w5500_config, &mac_config_spi);
        phy_spi[i] = esp_eth_phy_new_w5500(&phy_config_spi);
        
        esp_eth_config_t eth_config_spi = ETH_DEFAULT_CONFIG(mac_spi[i], phy_spi[i]);
        ESP_ERROR_CHECK(esp_eth_driver_install(&eth_config_spi, &eth_control));

        /* O módulo SPI Ethernet pode não ter um endereço MAC gravado na fábrica,
           02:00:00 é uma faixa de OUI administrado localmente e não deve ser usada, exceto para testes em uma LAN sob seu controle. */
        ESP_ERROR_CHECK(esp_eth_ioctl(eth_control, ETH_CMD_S_MAC_ADDR, (uint8_t[]) {
            0x02, 0x00, 0x00, 0x12, 0x34, 0x56 + i
        }));

        esp_netif_dhcpc_stop(eth_netif_spi[i]);
        esp_netif_set_ip_info(eth_netif_spi[i], &ip_info);
        
        // Anexa o driver Ethernet à pilha TCP/IP
        ESP_ERROR_CHECK(esp_netif_attach(eth_netif_spi[i], esp_eth_new_netif_glue(eth_control)));
    }

    // Registra manipuladores de eventos definidos pelo usuário
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));

    /* Inicia a máquina de estados do driver Ethernet */
    for (int i = 0; i < CONFIG_EXAMPLE_SPI_ETHERNETS_NUM; i++) {
        ESP_ERROR_CHECK(esp_eth_start(eth_control));
    }
}

// Define a velocidade da rede e realiza uma reinicialização rígida, se necessário
void set_net(eth_speed_t speed, bool hard_reset) {
    bool nego = false;
    bool nego_t = true;

    ESP_ERROR_CHECK(esp_eth_stop(eth_control));
    ESP_ERROR_CHECK(esp_eth_ioctl(eth_control, 5, &nego));
    ESP_ERROR_CHECK(esp_eth_ioctl(eth_control, 7, &speed));
    if (speed == ETH_SPEED_100M)
        ESP_ERROR_CHECK(esp_eth_ioctl(eth_control, 5, &nego_t));
    ESP_ERROR_CHECK(esp_eth_start(eth_control));
}

// Suspende a execução
void suspend() {
    if (eco_mode == Enabled)
        set_net(ETH_SPEED_100M, false);

    vTaskSuspend(xHandleSleep);
}

// Retoma a execução
void resume() {
    if (eco_mode == Enabled)
        set_net(ETH_SPEED_10M, false);
    
    vTaskResume(xHandleSleep);
}

// Inicia uma chamada
void call() {
    if (call_started)
        return;

    ESP_LOGI(TAG, "[ * ] [Play] Event 2");
    call_started = true;
    xEventGroupClearBits(ethernet_event_group, REGISTERED_BIT);
    suspend();
    audio_player_int_tone_play(tone_uri[TONE_TYPE_CALLING]);
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    xEventGroupSetBits(ethernet_event_group, REQUEST_CALL);
}

static esp_err_t input_key_service_cb(periph_service_handle_t handle, periph_service_event_t *evt, void *ctx)
{
    // Verifica se a variável 'started' é falsa
    if (!started)
        return ESP_OK;  // Retorna com ESP_OK se 'started' for falso, impedindo o processamento de eventos

    int player_volume;
    periph_service_handle_t wifi_serv = (periph_service_handle_t) ctx;

    // Verifica o tipo de evento (clique liberado ou pressionado)
    if (evt->type == INPUT_KEY_SERVICE_ACTION_CLICK_RELEASE) {
        // Comentários desativados indicam ações para eventos de teclas específicas
        // switch ((int)evt->data) {
        //     case INPUT_KEY_USER_ID_REC:
        //         // Ação para a tecla REC
        //         ESP_LOGI(TAG, "[ * ] [Play] Event 1");
        //         player_volume = 80;
        //         av_audio_set_vol(av_stream, player_volume);
        //         request_call = true;
        //         break;
        //     case INPUT_KEY_USER_ID_PLAY:
        //         // Ação para a tecla PLAY
        //         ESP_LOGI(TAG, "[ * ] [Play] input key event");
        //         audio_player_int_tone_stop();
        //         esp_rtc_answer(esp_sip);
        //         break;
        //     case INPUT_KEY_USER_ID_MODE:
        //     case INPUT_KEY_USER_ID_SET:
        //         // Ação para as teclas MODE ou SET
        //         audio_player_int_tone_stop();
        //         esp_rtc_bye(esp_sip);
        //         break;
        //     case INPUT_KEY_USER_ID_VOLUP:
        //         // Ação para a tecla VOLUP
        //         ESP_LOGD(TAG, "[ * ] [Vol+] input key event");
        //         av_audio_get_vol(av_stream, &player_volume);
        //         player_volume += 10;
        //         if (player_volume > 100) {
        //             player_volume = 100;
        //         }
        //         av_audio_set_vol(av_stream, player_volume);
        //         ESP_LOGI(TAG, "[ * ] Volume set to %d %%", player_volume);
        //         break;
        //     case INPUT_KEY_USER_ID_VOLDOWN:
        //         // Ação para a tecla VOLDOWN
        //         ESP_LOGD(TAG, "[ * ] [Vol-] input key event");
        //         av_audio_get_vol(av_stream, &player_volume);
        //         player_volume -= 10;
        //         if (player_volume < 0) {
        //             player_volume = 0;
        //         }
        //         av_audio_set_vol(av_stream, player_volume);
        //         ESP_LOGI(TAG, "[ * ] Volume set to %d %%", player_volume);
        //         break;
        // }
    } else if (evt->type == INPUT_KEY_SERVICE_ACTION_PRESS) {
        // Para eventos de tecla pressionada
        switch ((int)evt->data) {
            // Comentários desativados indicam ação para a tecla PLAY pressionada
            // case INPUT_KEY_USER_ID_SET:
            //     is_smart_config = true;
            //     sip_service_stop(esp_sip);
            //     wifi_service_setting_start(wifi_serv, 0);
            //     // audio_player_int_tone_play(tone_uri[TONE_TYPE_MAX]);
            //     break;
            case INPUT_KEY_USER_ID_PLAY:
                // Ação para a tecla PLAY pressionada, chamando a função 'call'
                call();
                break;
        }
    }

    return ESP_OK;  // Retorna ESP_OK após o processamento dos eventos
}

// Função que executa um loop para entrar em modo de light sleep
static void light_sleep_task(void *pvParameter)
{
    while (true) {
        ESP_LOGI(TAG, "DORMINDO");  // Registra uma mensagem indicando que o sistema está em light sleep
        esp_light_sleep_start();  // Inicia o modo de light sleep
        /* Determina a razão para acordar */
        const char* wakeup_reason;
        switch (esp_sleep_get_wakeup_cause()) {
            case ESP_SLEEP_WAKEUP_TIMER:
                wakeup_reason = "timer";  // O sistema acordou devido a um temporizador
                printf("timer \n");
                break;
            case ESP_SLEEP_WAKEUP_GPIO:
                wakeup_reason = "pin";  // O sistema acordou devido a uma mudança de nível em um pino GPIO
                vTaskSuspend(xHandleSleep);  // Suspende uma tarefa chamada xHandleSleep
                printf("BOTAO \n");
                break;
            default:
                wakeup_reason = "other";  // O sistema acordou por outro motivo
                break;
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);  // Aguarda por 100ms antes de entrar novamente em light sleep
    }
}

// Função para abrir o armazenamento NVS
esp_err_t storage_open(nvs_handle_t *out) {
    esp_err_t err;

    err = nvs_open("nvs", NVS_READWRITE, out);  // Abre o armazenamento NVS para leitura e gravação

    if (err != ESP_OK) {
        printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));  // Registra uma mensagem de erro, se houver
    } else {
        printf("Open Done\n");  // Registra uma mensagem indicando que a abertura foi concluída com sucesso
    }
    
    return err;
}

// Função para fechar o armazenamento NVS
esp_err_t storage_close(nvs_handle_t out) {
    esp_err_t err;

    err = nvs_commit(out);  // Confirma as alterações no armazenamento NVS
    printf((err != ESP_OK) ? "Failed!\n" : "Commit Done\n");  // Registra se a confirmação foi bem-sucedida ou não

    nvs_close(out);  // Fecha o handle do armazenamento NVS
    
    return err;
}
// Função para inicializar o sistema de armazenamento NVS
esp_err_t init_nvs() {
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        // A partição NVS foi truncada e precisa ser apagada
        // Tentar novamente nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    if (err != ESP_OK)
            return err;

    // Abre o armazenamento NVS
    err = storage_open(&nvs_storage);

    if (err != ESP_OK)
        return err;

    // Define tamanhos de variáveis para recuperar do NVS
    size_t len_sip = sizeof(sip);
    size_t len_addr = sizeof(addr);
    size_t len_call_sip = sizeof(call_sip);
    size_t len_network_config = sizeof(esp_netif_ip_info_t);
    size_t len_eco_config = sizeof(EconomicMode);

    // Recupera strings e valores do NVS
    ESP_LOGI("GET STORAGE", "sip_pass");
    err = nvs_get_str(nvs_storage, "sip_pass", sip, &len_sip);
    if (err != ESP_OK)
        ESP_LOGI("GET STORAGE", "sip_pass: %d", err);

    ESP_LOGI("GET STORAGE", "addr_sip");
    err = nvs_get_str(nvs_storage, "addr_sip", addr, &len_addr);
    if (err != ESP_OK)
        ESP_LOGI("GET STORAGE", "addr_sip: %d", err);

    ESP_LOGI("GET STORAGE", "port_sip");
    err = nvs_get_u16(nvs_storage, "port_sip", &port);
    if (err != ESP_OK)
        ESP_LOGI("GET STORAGE", "port_sip: %d", err);

    ESP_LOGI("GET STORAGE", "call_sip");
    err = nvs_get_str(nvs_storage, "call_sip", call_sip, &len_call_sip);
    if (err != ESP_OK)
        ESP_LOGI("GET STORAGE", "call_sip: %d", err);

    ESP_LOGI("GET STORAGE", "audio_volume");
    err = nvs_get_u8(nvs_storage, "audio_volume", &player_volume);
    if (err != ESP_OK)
        ESP_LOGI("GET STORAGE", "audio_volume: %d", err);

    ESP_LOGI("GET STORAGE", "network_config");
    err = nvs_get_blob(nvs_storage, "network_config", &ip_info, &len_network_config);
    if (err != ESP_OK)
        ESP_LOGI("GET STORAGE", "network_config: %d", err);

    ESP_LOGI("GET STORAGE", "eco_config");
    err = nvs_get_blob(nvs_storage, "eco_config", &eco_mode, &len_eco_config);
    if (err != ESP_OK)
        ESP_LOGI("GET STORAGE", "eco_config: %d", err);

    // Fecha o armazenamento NVS
    storage_close(nvs_storage);

    return ESP_OK;
}

// Função para retomar após um timeout
void resume_timeout() {
    vTaskDelay(30000 / portTICK_PERIOD_MS);
    
    if (!call_started)
        vTaskResume(xHandleSleep);

    vTaskDelete(NULL);
}

// Função para inicializar o sistema web
void init_web() {
    ESP_LOGI("CALL", "Web INIT");
    vTaskSuspend(xHandleSleep);

    // Cria uma nova tarefa para retomar após um timeout
    xTaskCreate(resume_timeout, "timeout_web_task", 1024, NULL, 2, NULL);
}

// Função para limitar um valor dentro de um intervalo
int clamp(int value, int min, int max) {
    return value > max ? max : value < min ? min : value;
}

void init_interface(Command cmd, DataValue *data, char *response) {
    esp_err_t err;
    
    switch (cmd)
    {
    case Login:
        ESP_LOGI("CALL", "Login");
        break;

    case Reboot:
        esp_restart();
        break;
        
    case FactoryReset:
        esp_restart();
        break;

    case Maintenance:
        ESP_LOGI("CALL", "Maintenance");
        break;

    case Telemetry:
        ESP_LOGI("CALL", "Telemetry");
        break;

    case Call:
        vTaskSuspend(xHandleSleep);
        call();
        break;

    case EndCall:
        esp_rtc_bye(esp_sip);
        break;

    case Gpio:
        ESP_LOGI("CALL", "Gpio");
        break;

    case AudioTest:
        ESP_LOGI("CALL", "AudioTest");
        break;

    case SpeakerVolume:
        if (data->volume == NULL)
            break;

        break;

    case SetAudioVolume:
        if (data->volume == NULL)
            break;
    
        player_volume = (*data->volume)->audio;
        player_volume = clamp(player_volume, 0, 100);
    
        av_audio_set_vol(av_stream, player_volume);

        storage_open(&nvs_storage);

        err = nvs_set_u8(nvs_storage, "audio_volume", player_volume);
        if (err != ESP_OK)
            ESP_LOGI("SET STORAGE", "audio_volume: %d", err);

        storage_close(nvs_storage);

        break;

    case GetAudioVolume: ;
        int gvol;
        av_audio_get_vol(av_stream, &gvol);
        gvol = clamp(gvol, 0, 100);

        itoa(gvol, response, 10);

        break;

    case Ping:
        strcpy(response, "Pong");
        break;

    case Update:
        ESP_LOGI("CALL", "Update");
        break;

    case Version:
        ESP_LOGI("CALL", "Version");
        break;
    
    default:
        ESP_LOGI("CALL", "Default");
        break;
    }
}

void init_interface_ws(Command cmd, DataValue *data, char *response) {
    esp_err_t err;

    switch (cmd)
    {
    case Login:
        ESP_LOGI("CALL", "Login");

        LoginFields *login = (*data->login);
        bool user = strcmp(login->user, "Tracevia") == 0;
        bool pass = check_pass("Trac3via@20", login->pass);

        if (user && pass) {
            strcpy(response, "{ \
                \"type\": \"Accept\" \
            }");
        } else {
            strcpy(response, "{ \
                \"type\": \"Disconnected\" \
            }");
        }
        
        break;

    case Reboot:
        esp_restart();
        break;
        
    case FactoryReset:
        esp_restart();
        break;

    case Maintenance:
        ESP_LOGI("CALL", "Maintenance");
        break;

    case Telemetry:
        ESP_LOGI("CALL", "Telemetry");
        break;

    case Call:
        vTaskSuspend(xHandleSleep);
        call();
        break;

    case EndCall:
        esp_rtc_bye(esp_sip);
        break;

    case Gpio:
        ESP_LOGI("CALL", "Gpio");
        break;

    case AudioTest:
        ESP_LOGI("CALL", "AudioTest");
        break;

    case SpeakerVolume:
        if (data->volume == NULL)
            break;

        break;

    case SetAudioVolume:
        if (data->volume == NULL)
            break;
    
        player_volume = (*data->volume)->audio;
        player_volume = clamp(player_volume, 0, 100);
    
        av_audio_set_vol(av_stream, player_volume);

        storage_open(&nvs_storage);

        err = nvs_set_u8(nvs_storage, "audio_volume", player_volume);
        if (err != ESP_OK)
            ESP_LOGI("SET STORAGE", "audio_volume: %d", err);

        storage_close(nvs_storage);

        break;

    case GetAudioVolume: ;
        int gvol;
        av_audio_get_vol(av_stream, &gvol);
        gvol = clamp(gvol, 0, 100);

        sprintf(response, "{ \
            \"type\": \"Audio\", \
            \"data\": { \
                \"volume\": %d, \
                \"microfone\": 0 \
            } \
        }", gvol);

        break;

    case SetSip:
        if (data->string == NULL)
            break;

        SipConfig *sip_c = (*data->sip);

        strcpy(sip, sip_c->sip);
        strcpy(addr, sip_c->addr);
        port = sip_c->port;
        strcpy(call_sip, sip_c->call);

        storage_open(&nvs_storage);

        err = nvs_set_str(nvs_storage, "sip_pass", sip);
        if (err != ESP_OK)
            ESP_LOGI("SET STORAGE", "sip_pass: %d", err);

        err = nvs_set_str(nvs_storage, "addr_sip", addr);
        if (err != ESP_OK)
            ESP_LOGI("SET STORAGE", "addr_sip: %d", err);

        err = nvs_set_u16(nvs_storage, "port_sip", port);
        if (err != ESP_OK)
            ESP_LOGI("SET STORAGE", "port_sip: %d", err);

        err = nvs_set_str(nvs_storage, "call_sip", call_sip);
        if (err != ESP_OK)
            ESP_LOGI("SET STORAGE", "call_sip: %d", err);

        storage_close(nvs_storage);

        sip_service_stop(esp_sip);

        char *s = malloc(100);
        sprintf(s, "udp://%s@%s:%d", sip, addr, port);
        
        esp_sip = sip_service_start(av_stream, s);

        break;

    case GetSip:
        sprintf(response, "{ \
            \"type\": \"Sip\", \
            \"data\": { \
                \"sip\": \"%s\", \
                \"addr\": \"%s\", \
                \"port\": %d, \
                \"call\": \"%s\" \
            } \
        }", sip, addr, port, call_sip);

        break;
        
    case SetNetwork:
        if (data->net == NULL)
            break;

        NetworkConfig *network = (*data->net);

        ip_info.ip.addr = network->addr;
        ip_info.netmask.addr = network->mask;
        ip_info.gw.addr = network->gate;
        eco_mode = network->eco;

        storage_open(&nvs_storage);

        size_t len = sizeof(esp_netif_ip_info_t);
        size_t len_eco = sizeof(EconomicMode);

        err = nvs_set_blob(nvs_storage, "network_config", &ip_info, len);
        if (err != ESP_OK)
            ESP_LOGI("SET STORAGE", "network_config: %d", err);

        err = nvs_set_blob(nvs_storage, "eco_config", &eco_mode, len_eco);
        if (err != ESP_OK)
            ESP_LOGI("SET STORAGE", "eco_config: %d", err);

        storage_close(nvs_storage);

        esp_restart();

        break;
    
    case GetNetwork: ;
        char *eco;
        switch (eco_mode)
        {
            case Enabled:
                eco = "Enable";
                break;

            case Ultra:
                eco = "Full";
                break;
            
            default:
                eco = "Disable";
                break;
        }

        sprintf(response, "{ \
            \"type\": \"Network\", \
            \"data\": { \
                \"addr\": \"%d.%d.%d.%d\", \
                \"mask\": \"%d.%d.%d.%d\", \
                \"gate\": \"%d.%d.%d.%d\", \
                \"mode\": \"%s\" \
            } \
        }", IP2STR(&ip_info.ip), IP2STR(&ip_info.netmask), IP2STR(&ip_info.gw), eco);

        break;

    case Ping:
        strcpy(response, "Pong");
        break;

    case Update:
        ESP_LOGI("CALL", "Update");
        break;

    case Version:
        ESP_LOGI("CALL", "Version");
        break;
    
    case Custom:
        ESP_LOGI("CALL", "Custom");
        char *custom = (*data->string);

        ESP_LOGI("CUSTOM", "%s", custom);

        break;
        
    default:
        ESP_LOGI("CALL", "Default");
        break;
    }
}

void app_main()
{
    // Configura o nível de log para informações gerais e desativa o log de elementos de áudio.
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("AUDIO_ELEMENT", ESP_LOG_ERROR);
    AUDIO_MEM_SHOW(TAG);

    // Inicializa o armazenamento NVS.
    ESP_ERROR_CHECK(init_nvs());

    // Inicializa a interface de rede (compatível com diferentes versões do ESP-IDF).
#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0))
    ESP_ERROR_CHECK(esp_netif_init());
#else
    tcpip_adapter_init();
#endif

    // Cria um grupo de eventos para eventos de Ethernet.
    ethernet_event_group = xEventGroupCreate();
    xEventGroupClearBits(ethernet_event_group, 0xFF);

    // Inicializa e configura periféricos, como botões e teclas.
    ESP_LOGI(TAG, "[1.0] Initialize peripherals management");
    periph_service_handle_t wifi_serv = NULL;
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);

    ESP_LOGI(TAG, "[1.1] Initialize and start peripherals");
    audio_board_key_init(set);

    // Cria e inicia o serviço de detecção de teclas de entrada.
    ESP_LOGI(TAG, "[1.2] Create and start input key service");
    input_key_service_cfg_t input_cfg = INPUT_KEY_SERVICE_DEFAULT_CONFIG();
    input_cfg.handle = set;
    input_cfg.based_cfg.task_stack = 4 * 1024;
    input_cfg.based_cfg.extern_stack = true;
    periph_service_handle_t input_ser = input_key_service_create(&input_cfg);
    input_key_service_info_t input_key_info[] = INPUT_KEY_DEFAULT_INFO();
    input_key_service_add_key(input_ser, input_key_info, INPUT_KEY_NUM);
    periph_service_set_callback(input_ser, input_key_service_cb, wifi_serv);

    #if (DEBUG_AEC_INPUT || DEBUG_AEC_OUTPUT)
    audio_board_sdcard_init(set, SD_MODE_1_LINE);
    #endif

    ESP_LOGI(TAG, "[ 2 ] Initialize av stream");
    av_stream_config_t av_stream_config = {
        .algo_mask = ALGORITHM_STREAM_DEFAULT_MASK,
        .acodec_samplerate = AUDIO_CODEC_SAMPLE_RATE,
        .acodec_type = AV_ACODEC_G711A,
        .vcodec_type = AV_VCODEC_NULL,
        .hal = {
            .audio_samplerate = AUDIO_HAL_SAMPLE_RATE,
            .audio_framesize = PCM_FRAME_SIZE,
        },
    };
    av_stream = av_stream_init(&av_stream_config);
    AUDIO_NULL_CHECK(TAG, av_stream, return);

    // Inicializa o player de tons.
    ESP_LOGI(TAG, "[ 3 ] Initialize tone player");
    audio_player_int_tone_init(16000, I2S_CHANNELS, I2S_DEFAULT_BITS);

    // Cria uma instância do serviço Ethernet.
    ESP_LOGI(TAG, "[ 4 ] Create Eth service instance");
    // Inicializa a interface de rede TCP/IP (deve ser chamada apenas uma vez na aplicação).
    ESP_ERROR_CHECK(esp_netif_init());
    // Cria um loop de eventos padrão em segundo plano.
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    av_audio_set_vol(av_stream, player_volume);

    // Inicializa a rede.
    init_network();

    // Aguarda a conexão à Ethernet e configura timers e GPIO.
    xEventGroupWaitBits(ethernet_event_group, CONNECTED_BIT, false, true, 20000 / portTICK_PERIOD_MS);
    example_register_timer_wakeup();
    example_register_gpio_wakeup();

    // Cria uma tarefa para a função light_sleep_task.
    xTaskCreate(light_sleep_task, "light_sleep_task", 4096, NULL, 2, &xHandleSleep);

    // Configura a velocidade da Ethernet com base no modo econômico.
    if (eco_mode == Ultra)
        set_net(ETH_SPEED_10M, false);

    // Inicia a lógica principal e ativa a flag "started".
    resume();
    started = true;

    // Configura servidores web e inicia clientes HTTP e WebSocket.
    Urls *urls = create_url();
    web_server_default(urls, &init_web);
    ClientHTTP *client = start_client(urls, 80, 5);
    Interface *interface = create_interface(4321, false);
    Interface *interface_ws = create_interface(4322, true);
    set_interface_callback(interface, init_interface);
    set_interface_callback(interface_ws, init_interface_ws);
    run_interface(interface);
    run_interface(interface_ws);

    char port[6];

    while (1) {
        // Aguarda a ativação da flag REQUEST_CALL no grupo de eventos Ethernet.
        xEventGroupWaitBits(ethernet_event_group, REQUEST_CALL, false, true, portMAX_DELAY);
        count_s++;

        while ((xEventGroupGetBits(ethernet_event_group) & REQUEST_CALL) > 0) {
            ESP_LOGI("CALL", "TRYING CALL");

            // Ativa tom de chamada e aguarda o registro na rede.
            xEventGroupWaitBits(ethernet_event_group, REGISTERED_BIT, false, true, 5000 / portTICK_PERIOD_MS);

            if ((xEventGroupGetBits(ethernet_event_group) & REGISTERED_BIT) > 0) {
                esp_rtc_call(esp_sip, itoa(call_sip, port, 10));
                vTaskDelay(500 / portTICK_PERIOD_MS);
            } else {
                xEventGroupClearBits(ethernet_event_group, REQUEST_CALL | INIT_CALL);
                call_started = false;
                audio_player_int_tone_play(tone_uri[TONE_TYPE_NO_ANSWER]);
                vTaskDelay(16000 / portTICK_PERIOD_MS);
                esp_restart();
            }
        }
        audio_sys_get_real_time_stats();
        AUDIO_MEM_SHOW(TAG);

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
