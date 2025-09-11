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

// ---- Lógica de serpientes y colisiones ----
void eraseSnakeTail(const SnakeState& s) {
    if (!s.body.empty()) clearCell(s.body.back().x, s.body.back().y);
}
void advanceSnake(SnakeState& s) {
    if (!s.alive || s.dir==-1 || s.body.empty()) return;
    Coord head = s.body[0];
    switch (s.dir) { case 0: head.y--; break; case 1: head.y++; break;
                     case 2: head.x--; break; case 3: head.x++; break; }
    if (!insideField(head.x, head.y) || onTrap(head.x, head.y)) { s.alive=false; return; }
    for (auto& c: s.body) if (head.x==c.x && head.y==c.y) { s.alive=false; return; }
    SnakeState& other = (s.id==1 ? s2 : s1);
    for (auto& c: other.body) if (head.x==c.x && head.y==c.y) { s.alive=false; return; }

    bool ate = (head.x==food.x && head.y==food.y);
    if (ate) { s.score += 7; } else {
        eraseSnakeTail(s);
        for (int i=(int)s.body.size()-1;i>0;--i) s.body[i]=s.body[i-1];
    }
    s.body[0] = head;
    if (ate) placeFood();
}

// ---- Hilos ----
void* th_snake(void* arg) {
    SnakeState* S = (SnakeState*)arg;
    while (true) {
        pthread_mutex_lock(&mtx_state);
        bool running = game_running;
        pthread_mutex_unlock(&mtx_state);
        if (!running) break;

        pthread_mutex_lock(&mtx_state);
        advanceSnake(*S);
        bool sAlive = S->alive;
        pthread_mutex_unlock(&mtx_state);

        pthread_mutex_lock(&mtx_scr);
        clear(); drawBorders(); drawFood(); drawTraps();
        drawSnake(s1); if (game_mode==2) drawSnake(s2);
        drawHUD(); refresh();
        pthread_mutex_unlock(&mtx_scr);

        if (!sAlive) { pthread_mutex_lock(&mtx_state); game_running=false; pthread_mutex_unlock(&mtx_state); break; }
        usleep(speed_ms * 1000);
    }
    return nullptr;
}
void* th_input(void* arg) {
    nodelay(stdscr, TRUE);
    while (true) {
        pthread_mutex_lock(&mtx_state); bool running=game_running; pthread_mutex_unlock(&mtx_state);
        if (!running) break;
        pthread_mutex_lock(&mtx_scr); int ch = getch(); pthread_mutex_unlock(&mtx_scr);
        if (ch != ERR) {
            pthread_mutex_lock(&mtx_state);
            if (ch=='w'||ch=='W') { if (s1.dir!=1) s1.dir=0; }
            else if (ch=='s'||ch=='S') { if (s1.dir!=0) s1.dir=1; }
            else if (ch=='a'||ch=='A') { if (s1.dir!=3) s1.dir=2; }
            else if (ch=='d'||ch=='D') { if (s1.dir!=2) s1.dir=3; }
            else if (ch==KEY_UP)    { if (s2.dir!=1) s2.dir=0; }
            else if (ch==KEY_DOWN)  { if (s2.dir!=0) s2.dir=1; }
            else if (ch==KEY_LEFT)  { if (s2.dir!=3) s2.dir=2; }
            else if (ch==KEY_RIGHT) { if (s2.dir!=2) s2.dir=3; }
            pthread_mutex_unlock(&mtx_state);
        }
        usleep(20 * 1000);
    }
    return nullptr;
}
void* th_food(void* arg) {
    int ticks=0;
    while (true) {
        pthread_mutex_lock(&mtx_state); bool running=game_running; pthread_mutex_unlock(&mtx_state);
        if (!running) break;
        if (++ticks>=80) { pthread_mutex_lock(&mtx_state); placeFood(); pthread_mutex_unlock(&mtx_state); ticks=0; }
        usleep(100 * 1000);
    }
    return nullptr;
}
void* th_timer(void* arg) {
    while (true) {
        sleep(1);
        pthread_mutex_lock(&mtx_state);
        if (!game_running) { pthread_mutex_unlock(&mtx_state); break; }
        time_left--;
        if (time_left<=0) {
            level_++; if (speed_ms>60) speed_ms-=10;
            time_left=TIMER_START; genTraps(max(0, level_-2));
        }
        pthread_mutex_unlock(&mtx_state);
    }
    return nullptr;
}
void* th_traps_regen(void* arg) {
    while (true) {
        pthread_mutex_lock(&mtx_state); bool running=game_running; int ntr=max(0, level_-2); pthread_mutex_unlock(&mtx_state);
        if (!running) break;
        sleep(3); pthread_mutex_lock(&mtx_state); genTraps(ntr); pthread_mutex_unlock(&mtx_state);
    }
    return nullptr;
}

// ---- Arranque de partida ----
void startGame(int mode) {
    game_mode=mode; resetGame();
    pthread_t tS1,tS2,tIn,tFd,tTm,tTr;
    pthread_create(&tS1,nullptr,th_snake,&s1);
    if (mode==2) pthread_create(&tS2,nullptr,th_snake,&s2);
    pthread_create(&tIn,nullptr,th_input,nullptr);
    pthread_create(&tFd,nullptr,th_food,nullptr);
    pthread_create(&tTm,nullptr,th_timer,nullptr);
    pthread_create(&tTr,nullptr,th_traps_regen,nullptr);

    while (true) {
        pthread_mutex_lock(&mtx_state);
        bool running=game_running, bothDead=(!s1.alive)||(mode==2&&!s2.alive);
        pthread_mutex_unlock(&mtx_state);
        if (!running||bothDead) break;
        usleep(100*1000);
    }
    pthread_mutex_lock(&mtx_state); game_running=false; pthread_mutex_unlock(&mtx_state);
    pthread_join(tS1,nullptr); if (mode==2) pthread_join(tS2,nullptr);
    pthread_join(tIn,nullptr); pthread_join(tFd,nullptr); pthread_join(tTm,nullptr); pthread_join(tTr,nullptr);

    saveScore(mode==1?string("Snake 1P"):string("Snake 2P"));
    bool backToMenu=gameOverScreen(); if (!backToMenu) quit_program=true;
}

// ---- Inicialización ncurses y main ----
void init_ncurses() { initscr(); cbreak(); noecho(); keypad(stdscr,TRUE); nodelay(stdscr,FALSE); curs_set(0); srand(time(nullptr)); }
void end_ncurses() { endwin(); }

int main() {
    init_ncurses();
    showInstructions();
    while (!quit_program) {
        showMenu();
        int op=getch();
        if (op=='1') startGame(1);
        else if (op=='2') startGame(2);
        else if (op=='3') showDifficulty();
        else if (op=='4') showScores();
        else if (op=='5') showInstructions();
        else if (op=='6') quit_program=true;
    }
    end_ncurses();
    return 0;
}