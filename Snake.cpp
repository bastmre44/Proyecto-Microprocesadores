//---------------------------------------------------
// UNIVERSIDAD DEL VALLE DE GUATEMALA
// CC3086 - Programación de Microprocesadores
// Proyecto 1 – Snake
// Integrantes:
//  - Adriana Martínez
//  - Mishell Ciprian
//  - Belén Monterroso
//  - Pablo Cabrera
//---------------------------------------------------

#include <ncurses.h>
#include <pthread.h>
#include <unistd.h>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <string>
#include <fstream>
using namespace std;

// Estructuras y variables compartidas

struct Coordenada {
    int x, y;
};

struct EstadoSerpiente {
    vector<Coordenada> cuerpo;
    int direccion; // 0=arriba,1=abajo,2=izq,3=der
    int puntaje;
    bool viva;
};

EstadoSerpiente serp1, serp2;
Coordenada comida;
vector<Coordenada> trampas;

int modo_juego = 1;
int nivel = 1;
int velocidad = 150; // milisegundos
int tiempo_restante = 40;
bool juego_activo = true;

// Mutex
pthread_mutex_t mtx_estado = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mtx_pantalla = PTHREAD_MUTEX_INITIALIZER;




//Menú e Instrucciones 


void iniciar_ncurses() {
    initscr(); cbreak(); noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    curs_set(0);
    srand((unsigned)time(nullptr));
}

void finalizar_ncurses() {
    endwin();
}

void mostrar_menu() {
    pthread_mutex_lock(&mtx_pantalla);
    clear();
    mvprintw(2, 10, "===============================");
    mvprintw(3, 10, "        S N A K E   G A M E    ");
    mvprintw(4, 10, "===============================");
    mvprintw(6, 12, "1. Un jugador");
    mvprintw(7, 12, "2. Dos jugadores");
    mvprintw(8, 12, "3. Seleccionar dificultad");
    mvprintw(9, 12, "4. Puntajes destacados");
    mvprintw(10, 12, "5. Instrucciones");
    mvprintw(11, 12, "6. Salir");
    refresh();
    pthread_mutex_unlock(&mtx_pantalla);
}

void mostrar_instrucciones() {
    pthread_mutex_lock(&mtx_pantalla);
    clear();
    mvprintw(2, 5, "Instrucciones del Juego:");
    mvprintw(4, 5, "Jugador 1: WASD");
    mvprintw(5, 5, "Jugador 2: Flechas de direccion");
    mvprintw(6, 5, "Come la comida (@) para crecer");
    mvprintw(7, 5, "Evita las trampas (X), muros y tu propio cuerpo");
    mvprintw(9, 5, "Presiona cualquier tecla para volver al menu...");
    refresh();
    getch();
    pthread_mutex_unlock(&mtx_pantalla);
}


// LÓGICA DE JUEGO, HILOS Y MAIN
// ==================================================
int rnd(int a, int b) { return a + rand() % (b - a + 1); }
bool insideField(int x, int y) { return (x > 0 && x < W-1 && y > 0 && y < H-1); }

bool collisionWith(const vector<Coord>& body, int x, int y) {
    for (const auto& c: body) if (c.x==x && c.y==y) return true;
    return false;
}
bool cellOccupied(int x, int y) {
    return collisionWith(s1.body, x, y) || collisionWith(s2.body, x, y);
}
bool onTrap(int x, int y) {
    for (const auto& t: traps) if (t.x==x && t.y==y) return true;
    return false;
}
void placeFood() {
    while (true) {
        int x = rnd(1, W-2), y = rnd(1, H-2);
        if (!cellOccupied(x,y) && !onTrap(x,y)) { food = {x,y}; return; }
    }
}
void genTraps(int n) {
    traps.clear();
    for (int i=0;i<n;i++) {
        int x = rnd(1, W-2), y = rnd(1, H-2);
        if (!cellOccupied(x,y) && !(x==food.x && y==food.y))
            traps.push_back({x,y});
    }
}
void resetGame() {
    pthread_mutex_lock(&mtx_state);
    s1 = SnakeState(); s2 = SnakeState();
    s1.id = 1; s1.headCh='O'; s1.bodyCh='o'; s1.alive=true; s1.dir=3;
    s2.id = 2; s2.headCh='Q'; s2.bodyCh='q'; s2.alive=true; s2.dir=2;

    s1.body = {{W/2, H/2}, {W/2-1,H/2}, {W/2-2,H/2}};
    s2.body = {{W/2, H/2 - 5}, {W/2+1, H/2 -5}, {W/2+2, H/2 -5}};

    s1.score = s2.score = 0;
    level_ = 1;
    time_left = TIMER_START;
    placeFood();
    traps.clear();
    game_running = true;
    pthread_mutex_unlock(&mtx_state);
}
