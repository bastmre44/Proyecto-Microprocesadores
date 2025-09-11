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




//Menú + Instrucciones + setup.


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

