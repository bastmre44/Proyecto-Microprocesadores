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
bool show_next_level = false;

// Mutex
pthread_mutex_t mtx_state = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mtx_scr   = PTHREAD_MUTEX_INITIALIZER;

// ==================================================
// Representación gráfica 
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

int rnd(int a, int b) { 
    return a + rand() % (b - a + 1); 
}

bool insideField(int x, int y) { 
    return (x > 0 && x < W-1 && y > 0 && y < H-1); 
}

bool collisionWith(const vector<Coord>& body, int x, int y) {
    for (const auto& c: body) 
        if (c.x==x && c.y==y) return true;
    return false;
}

bool cellOccupied(int x, int y) {
    return collisionWith(s1.body, x, y) || collisionWith(s2.body, x, y);
}

bool onTrap(int x, int y) {
    for (const auto& t: traps) 
        if (t.x==x && t.y==y) return true;
    return false;
}

void placeFood() {
    while (true) {
        int x = rnd(1, W-2), y = rnd(1, H-2);
        if (!cellOccupied(x,y) && !onTrap(x,y)) { 
            food = {x,y}; 
            return; 
        }
    }
}

// Número máximo de trampas según el nivel
int maxTrapsForLevel() {
    return 3 + (level_ - 1); // nivel 1: 3 trampas, nivel 2: 4, etc.
}

// Agregar UNA trampa progresivamente
void addTrap() {
    int x, y;
    int tries = 0;
    do {
        x = rnd(1, W-2);
        y = rnd(1, H-2);
        tries++;
        if (tries > 500) break; // 
    } while (cellOccupied(x,y) || onTrap(x,y) || (x==food.x && y==food.y));

    if (insideField(x,y) && !cellOccupied(x,y) && !onTrap(x,y) && !(x==food.x && y==food.y)) {
        traps.push_back({x,y});
    }
}

// Reinicio de partida
void resetGame() {
    pthread_mutex_lock(&mtx_state);

    s1 = SnakeState(); 
    s2 = SnakeState();
    s1.id = 1; s1.headCh='O'; s1.bodyCh='o'; s1.alive=true; s1.dir=3;
    s2.id = 2; s2.headCh='#'; s2.bodyCh='+'; s2.alive=true; s2.dir=2;  // serpiente 2

    s1.body = {{W/2, H/2}, {W/2-1,H/2}, {W/2-2,H/2}};
    s2.body = {{W/2, H/2 - 5}, {W/2+1, H/2 -5}, {W/2+2, H/2 -5}};

    s1.score = s2.score = 0;
    level_ = 1;
    time_left = TIMER_START;
    placeFood();

    traps.clear();     
    addTrap();         // arranca con 1 trampa

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

    if (!insideField(head.x, head.y) || onTrap(head.x, head.y)) { s.alive=false; return; }
    for (const auto& c: s.body) if (head.x==c.x && head.y==c.y) { s.alive=false; return; }
    SnakeState& other = (s.id==1 ? s2 : s1);
    for (const auto& c: other.body) if (head.x==c.x && head.y==c.y) { s.alive=false; return; }

    bool ate = (head.x==food.x && head.y==food.y);
    if (!ate) eraseSnakeTail(s);

    for (int i=(int)s.body.size()-1;i>0;--i) s.body[i]=s.body[i-1];
    s.body[0] = head;

    if (ate) {
        s.score += 7;
        s.body.push_back(s.body.back());
        placeFood();
    }
}

// ==================================================
// Menú con título personalizado
// ==================================================
void showMenu() {
    pthread_mutex_lock(&mtx_scr);
    clear();

    mvprintw(1, 5,  "Jennifer E. Swofford");
    mvprintw(2, 5,  "                      __    __    __    __");
    mvprintw(3, 5,  "                     /  \\  /  \\  /  \\  /  \\");
    mvprintw(4, 5,  "____________________/  __\\/  __\\/  __\\/  __\\_____________________________");
    mvprintw(5, 5,  "___________________/  /__/  /__/  /__/  /________________________________");
    mvprintw(6, 5,  "                   | / \\   / \\   / \\   / \\  \\____");
    mvprintw(7, 5,  "                   |/   \\_/   \\_/   \\_/   \\    o \\");
    mvprintw(8, 5,  "                                           \\_____/--<");
    mvprintw(10,5,  ",adPPYba, 8b,dPPYba,  ,adPPYYba, 88   ,d8  ,adPPYba,");
    mvprintw(11,5,  "I8[    \"\" 88P'   `\"8a \"\"     `Y8 88 ,a8\"  a8P_____88");
    mvprintw(12,5,  " `\"Y8ba,  88       88 ,adPPPPP88 8888[    8PP\"\"\"\"\"\"\"");
    mvprintw(13,5,  "aa    ]8I 88       88 88,    ,88 88`\"Yba, \"8b,   ,aa");
    mvprintw(14,5,  "`\"YbbdP\"' 88       88 `\"8bbdP\"Y8 88   `Y8a `\"Ybbd8\"'");

    mvprintw(17, 22, "1. Un jugador");
    mvprintw(18, 22, "2. Dos jugadores");
    mvprintw(19, 22, "3. Seleccionar dificultad");
    mvprintw(20, 22, "4. Puntajes destacados");
    mvprintw(21, 22, "5. Instrucciones");
    mvprintw(22, 22, "6. Salir");

    refresh();
    pthread_mutex_unlock(&mtx_scr);
}

// ==================================================
// Instrucciones, dificultad, puntajes
// ==================================================
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
// Game Over
// ==================================================

bool gameOverScreen() {
    pthread_mutex_lock(&mtx_scr);
    clear();

    // ASCII del Game Over
    mvprintw(2, 5, "  __ _  __ _ _ __ ___   ___    _____   _____ _ __  ");
    mvprintw(3, 5, " / _` |/ _` | '_ ` _ \\ / _ \\  / _ \\ \\ / / _ \\ '__| ");
    mvprintw(4, 5, "| (_| | (_| | | | | | |  __/ | (_) \\ V /  __/ |    ");
    mvprintw(5, 5, " \\__, |\\__,_|_| |_| |_|\\___|  \\___/ \\_/ \\___|_|    ");
    mvprintw(6, 5, " |___/                                              ");

    // Puntajes
    mvprintw(9, 10, "Resultados finales:");
    mvprintw(11, 12, "Jugador 1: %d puntos", s1.score);
    if (game_mode == 2)
        mvprintw(12, 12, "Jugador 2: %d puntos", s2.score);
    mvprintw(14, 12, "Nivel alcanzado: %d", level_);

    // Opciones
    mvprintw(17, 10, "¿Qué deseas hacer?");
    mvprintw(19, 12, "1. Volver al menú");
    mvprintw(20, 12, "2. Salir");

    refresh();

    // Esperar opción
    int ch;
    do {
        ch = getch();
    } while (ch != '1' && ch != '2');

    pthread_mutex_unlock(&mtx_scr);

    return (ch == '1'); // true = volver al menú, false = salir
}


// ==================================================
// Hilos principales
// ==================================================
void* th_snake(void* arg) {
    SnakeState* S = (SnakeState*)arg;
    bool isPainter = (S->id == 1);
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
        if (!game_running) { 
            pthread_mutex_unlock(&mtx_state); 
            break; 
        }

        if (show_next_level) {
            counterNL++;
            if (counterNL>=2) { 
                show_next_level = false; 
                counterNL=0; 
            }
            pthread_mutex_unlock(&mtx_state);
            continue;
        }

        time_left--;
        if (time_left<=0) {
            level_++;
            if (speed_ms>60) speed_ms-=10;
            time_left = TIMER_START;
            // 🚫 antes llamabas a genTraps(...) aquí
            // ✅ ahora no, porque th_traps_regen se encarga
            show_next_level = true;
        }
        pthread_mutex_unlock(&mtx_state);
    }
    return nullptr;
}

void* th_traps_regen(void* arg) {
    while (true) {
        pthread_mutex_lock(&mtx_state);
        bool running = game_running;
        bool showNL  = show_next_level;
        int desired  = maxTrapsForLevel();
        int current  = (int)traps.size();
        pthread_mutex_unlock(&mtx_state);

        if (!running) break;

        if (!showNL && current < desired) {
            pthread_mutex_lock(&mtx_state);
            addTrap();
            pthread_mutex_unlock(&mtx_state);
            sleep(2); // aparecen una por una cada 2s
        } else {
            sleep(1);
        }
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
    showInstructions();

    while (!quit_program) {
        
        pthread_mutex_lock(&mtx_scr);
        nodelay(stdscr, FALSE);  // lectura bloqueante en menú
        flushinp();              
        pthread_mutex_unlock(&mtx_scr);

        showMenu();

        int op;
        do {
            op = getch();
        } while (op != '1' && op != '2' && op != '3' && op != '4' && op != '5' && op != '6');

        if (op=='1') startGame(1);
        else if (op=='2') startGame(2);
        else if (op=='3') showDifficulty();
        else if (op=='4') showScores();
        else if (op=='5') showInstructions();
        else if (op=='6') quit_program = true;
    }

    end_ncurses();
    return 0;
}


