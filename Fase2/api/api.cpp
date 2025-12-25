#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <crow.h>
// Definición dde codigos de las syscalls
#define SYS_KERNEL_LOGS 549
#define SYS_UPTIME_S 550
#define SYS_CPU_USAGE 551
#define SYS_RAM_USAGE 552

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

    app.port(18080).multithreaded().run();
    return 0;
}

/* 

Compilar: g++ main.cpp -lpthread -lpam -lpam_misc
Ejecutar: sudo ./a.out

*/