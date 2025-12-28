#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <crow.h>
#include <sys/syscall.h>
#include <string>
#include <security/pam_appl.h>
#include <security/pam_misc.h>
#include <filesystem>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>
#include <vector>
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
static const char* PAM_SERVICE_NAME = "passwd";

static int pam_conv_callback(int num_msg,
                       const struct pam_message **msg,
                       struct pam_response **resp,
                       void *appdata_ptr){

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
    pam_handle_t* pamhandler = nullptr;
    struct pam_conv conv { pam_conv_callback, (void*)password.c_str() };

    int pam_result = pam_start(PAM_SERVICE_NAME, username.c_str(), &conv, &pamhandler);
    if (pam_result != PAM_SUCCESS) {
        if (error_out) *error_out = pam_strerror(pamhandler, pam_result);
        printf("PAM start error: %d\n", pam_result);
        return false;
    }

    pam_result = pam_authenticate(pamhandler, 0);
    if (pam_result == PAM_SUCCESS){
        pam_result = pam_acct_mgmt(pamhandler, 0);
        //printf("pam_acct_mgmt returned: %d\n", pam_result);
    }

    bool ok = (pam_result == PAM_SUCCESS);
    if (!ok && error_out){
        *error_out = pam_strerror(pamhandler, pam_result);
        printf("PAM auth error: %s\n", error_out->c_str());
    }

    pam_end(pamhandler, pam_result);
    return ok;
}

// Función para verificar si el usuario pertenece al grupo 'sudo'
static bool is_user_admin(const std::string& username) {
    struct passwd *pw = getpwnam(username.c_str());
    if (!pw) return false;

    // Root siempre es admin (UID 0)
    if (pw->pw_uid == 0) return true;

    // Buscamos el grupo 'sudo' (común en Debian/Ubuntu) o 'wheel' (CentOS/Arch)
    const char* admin_groups[] = {"sudo", "wheel", "admin"};
    
    for (const char* group_name : admin_groups) {
        struct group *gr = getgrnam(group_name);
        if (!gr) continue;

        // Verificar si es el grupo principal del usuario
        if (pw->pw_gid == gr->gr_gid) return true;

        // Verificar grupos secundarios
        for (int i = 0; gr->gr_mem[i] != nullptr; i++) {
            if (username == gr->gr_mem[i]) return true;
        }
    }

    return false;
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

    CROW_ROUTE(app, "/login").methods(crow::HTTPMethod::POST)([](const crow::request& req){
        auto json = crow::json::load(req.body);
        if (!json || !json.has("username") || !json.has("password")) {
            return crow::response(400, "JSON con 'username' y 'password' requerido");
        }

        std::string username = json["username"].s();
        std::string password = json["password"].s();
        std::string pam_err;

        if (!pam_authenticate_user(username, password, &pam_err)) {
            crow::json::wvalue body;
            body["ok"] = false;
            body["error"] = "Credenciales incorrectas o error de PAM: " + pam_err;
            return crow::response(401, body);
        }
        bool is_admin = is_user_admin(username);

        crow::json::wvalue body;
        body["ok"] = true;
        body["username"] = username;
        body["is_admin"] = is_admin;
        return crow::response(200, body);
    });

    app.port(18080).multithreaded().run();
    return 0;
}

/* 

Compilar: g++ main.cpp -lpthread -lpam -lpam_misc
Ejecutar: sudo ./a.out

*/