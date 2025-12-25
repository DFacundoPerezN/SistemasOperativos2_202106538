#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <crow.h>
// Definición dde codigos de las syscalls
#define SYS_CPU_USAGE 551

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
        
        // Ejecutamos la syscall
        long res = syscall(SYS_CPU_USAGE, &cpu_usage);

        if (res != 0) {
            // Si la syscall falla, devolvemos un error 500
            return crow::response(500, "Error al ejecutar la syscall de uso de cpu");
        }

        // Cálculos
        // Suponiendo que cpu_usage viene en formato XXXX (ej. 1500 = 15.00%)
        float usage_percentage = cpu_usage / 100.0;

        // Construimos el JSON de respuesta
        crow::json::wvalue response;
        response["cpu_usage"] = cpu_usage;
        response["cpu_usage_percentage"] = usage_percentage;
        return crow::response(response);
        
    });

}

/* 

Compilar: g++ main.cpp -lpthread -lpam -lpam_misc
Ejecutar: sudo ./a.out

*/