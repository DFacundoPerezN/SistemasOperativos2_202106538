#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <crow.h>
#include <sys/syscall.h>
#include <string>
#include <security/pam_appl.h>
#include <security/pam_misc.h>
#include <filesystem>
// Definición dde codigos de las syscalls
#define SYS_KERNEL_LOGS 549
#define SYS_UPTIME_S 550
#define SYS_CPU_USAGE 551
#define SYS_RAM_USAGE 552
#define SYS_MY_ENCRYPT 553
#define SYS_MY_DECRYPT 554

// --- Middleware CORS ---
struct CORS {
    struct context {}; // Crow exige un 'context' aunque esté vacío

    void before_handle(crow::request& req, crow::response& res, context&) {
        if (req.method == crow::HTTPMethod::OPTIONS) {
            // Responder preflight inmediatamente
            res.add_header("Access-Control-Allow-Origin", "*");
            res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
            res.add_header("Access-Control-Allow-Methods", "GET,POST,PUT,PATCH,DELETE,OPTIONS");
            res.code = 204; // No Content
            res.end();
        }
    }

    void after_handle(crow::request&, crow::response& res, context&) {
        // Añadir siempre CORS a las respuestas normales
        res.add_header("Access-Control-Allow-Origin", "*");
        res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        res.add_header("Access-Control-Allow-Methods", "GET,POST,PUT,PATCH,DELETE,OPTIONS");
    }
};

// ------- PAM -------
static const char* PAM_SERVICE_NAME = "login";

static int pam_conv_cb(int num_msg,
                       const struct pam_message **msg,
                       struct pam_response **resp,
                       void *appdata_ptr)
{
    if (num_msg <= 0) return PAM_CONV_ERR;

    auto *responses =
        (pam_response*)calloc(num_msg, sizeof(pam_response));
    if (!responses) return PAM_CONV_ERR;

    const char *password = (const char *)appdata_ptr;

    for (int i = 0; i < num_msg; i++) {
        switch (msg[i]->msg_style) {
            case PAM_PROMPT_ECHO_OFF:
                responses[i].resp = strdup(password ? password : "");
                responses[i].resp_retcode = 0;
                break;
            case PAM_PROMPT_ECHO_ON:
            case PAM_ERROR_MSG:
            case PAM_TEXT_INFO:
                responses[i].resp = nullptr;
                responses[i].resp_retcode = 0;
                break;
            default:
                free(responses);
                return PAM_CONV_ERR;
        }
    }

    *resp = responses;
    return PAM_SUCCESS;
}

static bool pam_authenticate_user(const std::string& username,
                                  const std::string& password,
                                  std::string* error_out = nullptr)
{
    pam_handle_t* pamh = nullptr;
    struct pam_conv conv { pam_conv_cb, (void*)password.c_str() };

    int r = pam_start(PAM_SERVICE_NAME, username.c_str(), &conv, &pamh);
    if (r != PAM_SUCCESS) {
        if (error_out) *error_out = std::string(pam_strerror(pamh, r)) + "Contraseña incorrecta.";
        return false;
    }

    r = pam_authenticate(pamh, 0);
    if (r == PAM_SUCCESS)
        r = pam_acct_mgmt(pamh, 0);

    bool ok = (r == PAM_SUCCESS);
    if (!ok && error_out)
        *error_out = pam_strerror(pamh, r);

    pam_end(pamh, r);
    return ok;
}

int main() {
    crow::SimpleApp app;
    // Endpoint: /stats
    CROW_ROUTE(app, "/stats")([](){
        short cpu_usage = 0;
        short ram_usage = 0;
        
        // Ejecutamos la syscall
        long res = syscall(SYS_CPU_USAGE, &cpu_usage);

        if (res != 0) {
            // Si la syscall falla, devolvemos un error 500
            return crow::response(500, "Error al ejecutar la syscall de uso de cpu");
        }

        res = syscall(SYS_RAM_USAGE, &ram_usage);
        
        if (res != 0) {
            // Si la syscall falla, devolvemos un error 500
            return crow::response(500, "Error al ejecutar la syscall de uso de ram");
        }

        // Cálculos
        // Suponiendo que cpu_usage viene en formato XXXX (ej. 1500 = 15.00%)
        float usage_percentage = cpu_usage / 100.0;
        float ram_percentage = ram_usage / 100.0;

        // Construimos el JSON de respuesta
        crow::json::wvalue response;
        response["cpu_usage"] = cpu_usage;
        response["ram_usage"] = ram_usage;
        response["cpu_usage_percentage"] = usage_percentage;
        response["ram_usage_percentage"] = ram_percentage;
        return crow::response(response);
        
    });

    // endpoint: /uptime
    CROW_ROUTE(app, "/uptime")([](){
        unsigned int uptime = syscall(SYS_UPTIME_S);
        if (uptime < 0) {
            return crow::response(500, "Error al ejecutar la syscall de uptime");
        }
        crow::json::wvalue response;
        response["uptime_seconds"] = uptime;
        return crow::response(response);
    });

    //endpoint: /logs
    CROW_ROUTE(app, "/logs")([](){
        #define LOG_BUFFER_SIZE 1024*4
        char logs_buffer[LOG_BUFFER_SIZE];
        int actual_length = 0;
        //Inicialr el buffer para evitar basura en la memoria 
        memset(logs_buffer, 0, LOG_BUFFER_SIZE);
        int resultLogs = syscall(SYS_KERNEL_LOGS, logs_buffer, LOG_BUFFER_SIZE, &actual_length);
        if (resultLogs != 0) {
            return crow::response(500, "Error al ejecutar la syscall de logs");
        }
        logs_buffer[actual_length] = '\0'; // Asegurar que el buffer este null-terminated
        crow::json::wvalue response;
        response["logs"] = std::string(logs_buffer);
        return crow::response(response);
    });

    //endpoint: /encrypt
    CROW_ROUTE(app, "/encrypt").methods(crow::HTTPMethod::POST)([](const crow::request& req){
        auto body = crow::json::load(req.body);
        if (!body || !body.has("file_input") || !body.has("file_output") || !body.has("key") || !body.has("threads")) {
            return crow::response(400, "Invalid JSON");
        }

        // Convertimos primero a std::string explícitamente
        std::string raw_input = body["file_input"].s();
        std::string raw_output = body["file_output"].s();
        std::string raw_key = body["key"].s();
        int threads = body["threads"].i();

        // Ahora usamos filesystem::absolute con los std::string
        std::string file_input = std::filesystem::absolute(raw_input).string();
        std::string file_output = std::filesystem::absolute(raw_output).string();
        std::string key_path = std::filesystem::absolute(raw_key).string();

        // Llamada a la syscall usando los paths absolutos
        long result = syscall(SYS_MY_ENCRYPT, file_input, file_output, key_path, threads);

        crow::json::wvalue response;
        response["result"] = result;
        if (result >= 0){
            response["message"] = "Archivo encriptado exitosamente";
        } else {
            response["message"] = "Ocurrió un error en el kernel (Error: " + std::to_string(result) + ")";
            // Imprimimos para depurar qué rutas se están enviando exactamente
            printf("DEBUG - Input path: %s\n", file_input.c_str());
        }
        return crow::response(response);
    });

    //endpoint: /decrypt
    CROW_ROUTE(app, "/decrypt").methods(crow::HTTPMethod::POST)([](const crow::request& req){
        auto body = crow::json::load(req.body);
        if (!body || !body.has("file_input") || !body.has("file_output") || !body.has("key") || !body.has("threads")) {
            return crow::response(400, "Invalid JSON");
        }
        // Convertimos primero a std::string explícitamente
        std::string raw_input = body["file_input"].s();
        std::string raw_output = body["file_output"].s();
        std::string raw_key = body["key"].s();
        int threads = body["threads"].i();
        // Ahora usamos filesystem::absolute con los std::string
        std::string file_input = std::filesystem::absolute(raw_input).string();
        std::string file_output = std::filesystem::absolute(raw_output).string();
        std::string key_path = std::filesystem::absolute(raw_key).string();

        long result = syscall(SYS_MY_DECRYPT,  file_input.c_str(), file_output.c_str(), key_path.c_str(), threads);
        crow::json::wvalue response;
        
        response["result"] = result;
        if (result >= 0){
            response["message"] = "Archivo desencriptado exitosamente";
        } else {
            response["message"] = "Ocurrió un error en el kernel (Error: " + std::to_string(result) + ")";
        }
        return crow::response(response);
    });

    app.port(18080).multithreaded().run();
    return 0;
}

/* 

Compilar: g++ main.cpp -lpthread -lpam -lpam_misc
Ejecutar: sudo ./a.out

*/