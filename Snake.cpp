/*---------------------------------------------------
* UNIVERSIDAD DEL VALLE DE GUATEMALA
* CC3086 - Programación de Microprocesadores
* Proyecto 1 – Snake (ncurses + pthreads)
*
* Integrantes:
*  - Adriana Martínez  (24086)
*  - Mishell Ciprian   (231169)
*  - Belén Monterroso  (231497)
*  - Pablo Cabrera     (231156)
*---------------------------------------------------*/

#include <ncurses.h>
#include <pthread.h>
#include <unistd.h>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm> 
using namespace std;

// ==================================================
// Estructuras principales
// ==================================================
struct Coord { int x, y; };

struct SnakeState {
    vector<Coord> body;
    int dir = -1;         // 0=up,1=down,2=left,3=right
    int score = 0;
    bool alive = true;
    char headCh = 'O';
    char bodyCh = 'o';
    int id = 1;           // 1 o 2
};

// Constantes globales
const int W = 60;
const int H = 24;
const int HUDY = H + 1;
const int TIMER_START = 40;

// Variables globales
SnakeState s1, s2;
Coord food;
vector<Coord> traps;
int game_mode = 1;
int level_ = 1;
int speed_ms = 150;
int time_left = TIMER_START;
bool game_running = false;
bool quit_program = false;
bool show_next_level = false; // transición de niveles

// Mutex
pthread_mutex_t mtx_state = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mtx_scr   = PTHREAD_MUTEX_INITIALIZER;

// ==================================================
// Representación gráfica básica
// ==================================================
void drawBorders() {
    for (int x=0; x<W; ++x) { mvaddch(0, x, '-'); mvaddch(H-1, x, '-'); }
    for (int y=0; y<H; ++y) { mvaddch(y, 0, '|'); mvaddch(y, W-1, '|'); }
}

void drawHUD() {
    mvprintw(HUDY, 0, "Nivel:%d  Vel:%dms  Tiempo:%02d   P1:%d   P2:%d   Modo:%d",
             level_, speed_ms, time_left, s1.score, s2.score, game_mode);
}

void drawFood() { mvaddch(food.y, food.x, '@'); }
void drawTraps() { for (auto& t: traps) mvaddch(t.y, t.x, 'X'); }
void drawSnake(const SnakeState& s) {
    if (!s.body.empty()) {
        mvaddch(s.body[0].y, s.body[0].x, s.headCh);
        for (size_t i=1;i<s.body.size();++i) mvaddch(s.body[i].y, s.body[i].x, s.bodyCh);
    }
}
void clearCell(int x, int y) { mvaddch(y, x, ' '); }

// ==================================================
// Utilidades
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
    genTraps(level_-1);
    game_running = true;
    pthread_mutex_unlock(&mtx_state);
}

// ==================================================
// Movimiento serpiente
// ==================================================
void eraseSnakeTail(const SnakeState& s) {
    if (!s.body.empty()) clearCell(s.body.back().x, s.body.back().y);
}
void advanceSnake(SnakeState& s) {
    if (!s.alive || s.dir==-1 || s.body.empty()) return;

    Coord head = s.body[0];
    switch (s.dir) { case 0: head.y--; break; case 1: head.y++; break;
                     case 2: head.x--; break; case 3: head.x++; break; }

    // Colisiones con bordes/trampas
    if (!insideField(head.x, head.y) || onTrap(head.x, head.y)) { s.alive=false; return; }

    // Colisión consigo misma (sobre estado actual)
    for (const auto& c: s.body) if (head.x==c.x && head.y==c.y) { s.alive=false; return; }

    // Colisión con la otra serpiente
    SnakeState& other = (s.id==1 ? s2 : s1);
    for (const auto& c: other.body) if (head.x==c.x && head.y==c.y) { s.alive=false; return; }

    bool ate = (head.x==food.x && head.y==food.y);

    // Si NO comió, borra la cola ANTES de desplazar
    if (!ate) eraseSnakeTail(s);

    // Desplaza el cuerpo
    for (int i=(int)s.body.size()-1;i>0;--i) s.body[i]=s.body[i-1];
    s.body[0] = head;

    if (ate) {
        s.score += 7;
        s.body.push_back(s.body.back()); // crece
        placeFood();
    }
}

// ==================================================
// Menú, instrucciones, puntajes, dificultad
// ==================================================
void showMenu() {
    pthread_mutex_lock(&mtx_scr);
    clear();
    mvprintw(3, 20, "==============================");
    mvprintw(4, 20, "         S N A K E");
    mvprintw(5, 20, "==============================");
    mvprintw(8, 22, "1. Un jugador");
    mvprintw(9, 22, "2. Dos jugadores");
    mvprintw(10,22, "3. Seleccionar dificultad");
    mvprintw(11,22, "4. Puntajes destacados");
    mvprintw(12,22, "5. Instrucciones");
    mvprintw(13,22, "6. Salir");
    refresh();
    pthread_mutex_unlock(&mtx_scr);
}

void showInstructions() {
    pthread_mutex_lock(&mtx_scr);
    clear();
    mvprintw(2, 5, "Instrucciones del Juego:");
    mvprintw(4, 5, "Jugador 1: WASD");
    mvprintw(5, 5, "Jugador 2: Flechas");
    mvprintw(6, 5, "Comida: @  (+7 puntos)");
    mvprintw(7, 5, "Trampas: X  (pierdes)");
    mvprintw(8, 5, "Cada %ds sube el nivel con +velocidad y trampas.", TIMER_START);
    mvprintw(10,5, "Presiona cualquier tecla para ir al menu...");
    refresh();
    getch();
    pthread_mutex_unlock(&mtx_scr);
}

void showDifficulty() {
    pthread_mutex_lock(&mtx_scr);
    clear();
    mvprintw(4, 10, "Selecciona dificultad:");
    mvprintw(6, 12, "1. Facil   (200 ms)");
    mvprintw(7, 12, "2. Media   (150 ms)");
    mvprintw(8, 12, "3. Dificil (100 ms)");
    refresh();
    int ch = getch();
    if (ch=='1') speed_ms = 200;
    else if (ch=='2') speed_ms = 150;
    else if (ch=='3') speed_ms = 100;
    pthread_mutex_unlock(&mtx_scr);
}

void saveScore(const string& label) {
    ofstream f("scores.txt", ios::app);
    if (!f) return;
    time_t now = time(nullptr);
    f << label << " | lvl:" << level_ << " | p1:" << s1.score << " | p2:" << s2.score
      << " | " << ctime(&now);
}

void showScores() {
    pthread_mutex_lock(&mtx_scr);
    clear();
    mvprintw(1, 2, "=== Puntajes destacados ===");
    ifstream f("scores.txt");
    string line; int row=3;
    if (f) {
        vector<string> lines;
        while (getline(f, line)) lines.push_back(line);
        int start = max(0, (int)lines.size()-15);
        for (int i=start;i<(int)lines.size();++i) {
            mvprintw(row++, 2, "%s", lines[i].c_str());
        }
    } else {
        mvprintw(3,2,"(No se pudo abrir scores.txt)");
    }
    mvprintw(row+2,2,"Presiona una tecla...");
    refresh();
    getch();
    pthread_mutex_unlock(&mtx_scr);
}

// ==================================================
// GAME OVER simple
// ==================================================
bool gameOverScreen() {
    pthread_mutex_lock(&mtx_scr);
    clear();
    mvprintw(6, 15, "=== GAME OVER ===");
    mvprintw(8, 15, "P1: %d   P2: %d   (Nivel %d)", s1.score, s2.score, level_);
    mvprintw(10,15, "1. Volver al menu");
    mvprintw(11,15, "2. Salir");
    refresh();
    int ch = getch();
    pthread_mutex_unlock(&mtx_scr);
    return (ch=='1');
}

// ==================================================
// Hilos principales
// ==================================================
void* th_snake(void* arg) {
    SnakeState* S = (SnakeState*)arg;
    bool isPainter = (S->id == 1); // solo serpiente 1 dibuja
    while (true) {
        pthread_mutex_lock(&mtx_state);
        bool running = game_running;
        bool showNL = show_next_level;
        pthread_mutex_unlock(&mtx_state);
        if (!running) break;

        pthread_mutex_lock(&mtx_state);
        if (!showNL) advanceSnake(*S);
        bool sAlive = S->alive;
        pthread_mutex_unlock(&mtx_state);

        if (isPainter) {
            pthread_mutex_lock(&mtx_scr);
            clear();
            if (showNL) {
                mvprintw(H/2-1, (W/2)-5, "=== NEXT LEVEL ===");
                mvprintw(H/2+1, (W/2)-7, "Entrando al nivel %d...", level_);
            } else {
                drawBorders(); drawFood(); drawTraps();
                drawSnake(s1); if (game_mode==2) drawSnake(s2);
                drawHUD();
            }
            refresh();
            pthread_mutex_unlock(&mtx_scr);
        }

        if (!sAlive) break;
        usleep(speed_ms * 1000);
    }
    return nullptr;
}

void* th_input(void* arg) {
    nodelay(stdscr, TRUE);
    while (true) {
        pthread_mutex_lock(&mtx_state); 
        bool running=game_running, showNL=show_next_level; 
        pthread_mutex_unlock(&mtx_state);
        if (!running) break;
        if (showNL) { usleep(20*1000); continue; }
        pthread_mutex_lock(&mtx_scr); int ch=getch(); pthread_mutex_unlock(&mtx_scr);
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
    int counterNL = 0;
    while (true) {
        sleep(1);
        pthread_mutex_lock(&mtx_state);
        if (!game_running) { pthread_mutex_unlock(&mtx_state); break; }

        if (show_next_level) {
            counterNL++;
            if (counterNL>=2) { show_next_level = false; counterNL=0; }
            pthread_mutex_unlock(&mtx_state);
            continue;
        }

        time_left--;
        if (time_left<=0) {
            level_++;
            if (speed_ms>60) speed_ms-=10;
            time_left=TIMER_START;
            genTraps(max(0, level_-2));
            show_next_level = true;
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

// ==================================================
// Inicio y cierre de partida
// ==================================================
void startGame(int mode) {
    game_mode=mode; 
    resetGame();

    pthread_t tS1,tS2,tIn,tFd,tTm,tTr;
    pthread_create(&tS1,nullptr,th_snake,&s1);
    if (mode==2) pthread_create(&tS2,nullptr,th_snake,&s2);
    pthread_create(&tIn,nullptr,th_input,nullptr);
    pthread_create(&tFd,nullptr,th_food,nullptr);
    pthread_create(&tTm,nullptr,th_timer,nullptr);
    pthread_create(&tTr,nullptr,th_traps_regen,nullptr);

    while (true) {
        pthread_mutex_lock(&mtx_state);
        bool end1P = (game_mode==1 && !s1.alive);
        bool end2P = (game_mode==2 && !s1.alive && !s2.alive);
        bool endGame = end1P || end2P;
        pthread_mutex_unlock(&mtx_state);
        if (endGame) break;
        usleep(100*1000);
    }

    // ✅ Apagar bandera antes de join
    pthread_mutex_lock(&mtx_state);
    game_running = false;
    pthread_mutex_unlock(&mtx_state);

    pthread_join(tS1,nullptr); if (mode==2) pthread_join(tS2,nullptr);
    pthread_join(tIn,nullptr); pthread_join(tFd,nullptr); pthread_join(tTm,nullptr); pthread_join(tTr,nullptr);

    saveScore(mode==1?string("Snake 1P"):string("Snake 2P"));
    bool backToMenu=gameOverScreen(); 
    if (!backToMenu) quit_program=true;
}

// ==================================================
// ncurses init/fin y main
// ==================================================
void init_ncurses() { 
    initscr(); 
    cbreak(); 
    noecho(); 
    keypad(stdscr,TRUE); 
    nodelay(stdscr,FALSE); 
    curs_set(0); 
    srand(time(nullptr)); 
}
void end_ncurses() { endwin(); }

int main() {
    init_ncurses();
    showInstructions();  // muestra instrucciones al inicio

    while (!quit_program) {
        showMenu();
        int op = getch();
        if (op=='1') startGame(1);
        else if (op=='2') startGame(2);
        else if (op=='3') showDifficulty();
        else if (op=='4') showScores();
        else if (op=='5') showInstructions();
        else if (op=='6') quit_program = true;
        // ignora otras teclas
    }

    end_ncurses();
    return 0;
}
