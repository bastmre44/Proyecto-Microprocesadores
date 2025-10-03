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
*
*---------------------------------------------------*/

#include <ncurses.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <cmath>
using namespace std;

// ==================================================
// MECANISMOS DE SINCRONIZACIÓN
// ==================================================
pthread_mutex_t mtx_state = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mtx_scr   = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_game_event = PTHREAD_COND_INITIALIZER;

sem_t sem_food_ready;
sem_t sem_level_change;

// ==================================================
// ESTRUCTURAS PRINCIPALES
// ==================================================
struct Coord { int x, y; };

struct SnakeState {
    vector<Coord> body;
    int dir = -1;
    int score = 0;
    bool alive = true;
    char headCh = 'O';
    char bodyCh = 'o';
    int id = 1;
    int lives = 3;
};

struct GameTheme {
    char p1_head, p1_body;
    char p2_head, p2_body;
    char food_basic, food_special;
    char trap;
    char border_horizontal, border_vertical;
    char corner_tl, corner_tr, corner_bl, corner_br;
    char separator;
    string name;
};

struct PowerUp {
    Coord pos;
    int type; // 0=speed, 1=invincible, 2=score2x, 3=life
    char symbol;
    int duration;
    bool active;
};

struct FloatingText {
    string text;
    int x, y;
    int life;
};

struct Particle {
    float x, y;
    float vx, vy;
    char symbol;
    int life;
};

struct ScoreRecord {
    string mode;
    int level;
    int p1_score;
    int p2_score;
    string date;
};

// ==================================================
// CONSTANTES Y VARIABLES GLOBALES
// ==================================================
const int W = 60;
const int H = 24;
const int HUDY = H + 1;
const int TIMER_START = 40;

SnakeState s1, s2;
Coord food;
vector<Coord> traps;
vector<PowerUp> powerups;
vector<FloatingText> floating_texts;
vector<Particle> particles;

int game_mode = 1;
int level_ = 1;
int speed_ms = 150;
int time_left = TIMER_START;
bool game_running = false;
bool quit_program = false;
bool show_next_level = false;
bool game_paused = false;
int food_animation_frame = 0;
int combo_p1 = 0, combo_p2 = 0;

// ==================================================
// TEMAS VISUALES
// ==================================================
GameTheme THEME_CLASSIC = {
    'O', 'o', '#', '+', '@', '*', 'X',
    '-', '|', '+', '+', '+', '+', '|',
    "Retro Clasico"
};

GameTheme THEME_MODERN = {
    'O', 'o', '#', '+', '@', '*', 'X',
    '=', '|', '+', '+', '+', '+', '|',
    "Bloques Modernos"
};

GameTheme THEME_MINIMAL = {
    'O', 'o', 'Q', 'q', '@', '$', 'x',
    '-', '|', '+', '+', '+', '+', '|',
    "Minimalista"
};

GameTheme THEME_MATRIX = {
    '0', '1', 'X', 'x', '*', '%', '#',
    '=', '|', '+', '+', '+', '+', '|',
    "Matrix"
};

GameTheme THEME_FANTASY = {
    'O', 'o', '@', '+', '*', '$', 'X',
    '-', '|', '+', '+', '+', '+', '|',
    "Fantasia"
};

GameTheme current_theme = THEME_CLASSIC;
int theme_index = 0;
vector<GameTheme> available_themes = {
    THEME_CLASSIC, THEME_MODERN, THEME_MINIMAL, THEME_MATRIX, THEME_FANTASY
};

// ==================================================
// UTILIDADES
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

bool onPowerUp(int x, int y, PowerUp*& pu) {
    for (auto& p : powerups) {
        if (p.active && p.pos.x==x && p.pos.y==y) {
            pu = &p;
            return true;
        }
    }
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

int maxTrapsForLevel() {
    return 3 + (level_ - 1);
}

void addTrap() {
    int x, y, tries = 0;
    do {
        x = rnd(1, W-2);
        y = rnd(1, H-2);
        tries++;
        if (tries > 500) break;
    } while (cellOccupied(x,y) || onTrap(x,y) || (x==food.x && y==food.y));

    if (insideField(x,y) && !cellOccupied(x,y) && !onTrap(x,y)) {
        traps.push_back({x,y});
    }
}

void applyThemeToSnakes() {
    s1.headCh = current_theme.p1_head;
    s1.bodyCh = current_theme.p1_body;
    s2.headCh = current_theme.p2_head;
    s2.bodyCh = current_theme.p2_body;
}

void resetGame() {
    pthread_mutex_lock(&mtx_state);
    
    s1 = SnakeState(); 
    s2 = SnakeState();
    s1.id = 1; s1.alive=true; s1.dir=3; s1.lives=3;
    s2.id = 2; s2.alive=true; s2.dir=2; s2.lives=3;
    
    applyThemeToSnakes();
    
    s1.body = {{W/2, H/2}, {W/2-1,H/2}, {W/2-2,H/2}};
    s2.body = {{W/2, H/2 - 5}, {W/2+1, H/2 -5}, {W/2+2, H/2 -5}};
    
    s1.score = s2.score = 0;
    level_ = 1;
    time_left = TIMER_START;
    combo_p1 = combo_p2 = 0;
    
    placeFood();
    traps.clear();     
    powerups.clear();
    floating_texts.clear();
    particles.clear();
    addTrap();
    
    game_running = true;
    pthread_mutex_unlock(&mtx_state);
}

// ==================================================
// EFECTOS VISUALES
// ==================================================
void showFloatingText(string text, int x, int y) {
    FloatingText ft;
    ft.text = text;
    ft.x = x;
    ft.y = y;
    ft.life = 15;
    floating_texts.push_back(ft);
}

void createParticles(int x, int y, int count) {
    for (int i = 0; i < count; i++) {
        Particle p;
        p.x = x;
        p.y = y;
        p.vx = (rand() % 200 - 100) / 100.0;
        p.vy = (rand() % 200 - 100) / 100.0;
        p.symbol = '.';
        p.life = 10;
        particles.push_back(p);
    }
}

void showDeathAnimation(int x, int y) {
    pthread_mutex_lock(&mtx_scr);
    
    mvaddch(y, x, '*');
    refresh();
    usleep(100000);
    
    mvaddch(y-1, x, '*');
    mvaddch(y, x-1, '*');
    mvaddch(y, x, 'X');
    mvaddch(y, x+1, '*');
    mvaddch(y+1, x, '*');
    refresh();
    usleep(100000);
    
    for (int i = -1; i <= 1; i++) {
        for (int j = -1; j <= 1; j++) {
            mvaddch(y+i, x+j, '.');
        }
    }
    refresh();
    usleep(150000);
    
    for (int i = -2; i <= 2; i++) {
        for (int j = -2; j <= 2; j++) {
            mvaddch(y+i, x+j, ' ');
        }
    }
    refresh();
    
    pthread_mutex_unlock(&mtx_scr);
}

void spawnPowerUp() {
    PowerUp pu;
    int x, y;
    do {
        x = rnd(1, W-2);
        y = rnd(1, H-2);
    } while (cellOccupied(x,y) || onTrap(x,y));
    
    pu.pos = {x, y};
    pu.type = rand() % 4;
    pu.active = true;
    pu.duration = 200;
    
    switch(pu.type) {
        case 0: pu.symbol = 'S'; break; // Speed
        case 1: pu.symbol = 'I'; break; // Invincible
        case 2: pu.symbol = '$'; break; // Score 2x
        case 3: pu.symbol = 'H'; break; // Health
    }
    
    powerups.push_back(pu);
}

// ==================================================
// DIBUJO CON TEMAS
// ==================================================
void drawBorders() {
    for (int x = 1; x < W-1; ++x) {
        mvaddch(0, x, current_theme.border_horizontal);
        mvaddch(H-1, x, current_theme.border_horizontal);
    }
    for (int y = 1; y < H-1; ++y) {
        mvaddch(y, 0, current_theme.border_vertical);
        mvaddch(y, W-1, current_theme.border_vertical);
    }
    mvaddch(0, 0, current_theme.corner_tl);
    mvaddch(0, W-1, current_theme.corner_tr);
    mvaddch(H-1, 0, current_theme.corner_bl);
    mvaddch(H-1, W-1, current_theme.corner_br);
}

void drawFood() {
    char food_chars[] = {'o', 'O', '@', 'O'};
    attron(A_BOLD);
    mvaddch(food.y, food.x, food_chars[food_animation_frame % 4]);
    attroff(A_BOLD);
}

void drawTraps() {
    for (auto& t: traps) {
        if (time_left % 2 == 0) attron(A_BOLD);
        mvaddch(t.y, t.x, current_theme.trap);
        if (time_left % 2 == 0) attroff(A_BOLD);
    }
}

void drawPowerUps() {
    for (auto& pu : powerups) {
        if (pu.active) {
            if ((pu.duration / 10) % 2 == 0) attron(A_REVERSE);
            mvaddch(pu.pos.y, pu.pos.x, pu.symbol);
            if ((pu.duration / 10) % 2 == 0) attroff(A_REVERSE);
        }
    }
}

char getDirectionalHead(int dir) {
    switch(dir) {
        case 0: return '^';
        case 1: return 'v';
        case 2: return '<';
        case 3: return '>';
        default: return 'O';
    }
}

void drawSnake(const SnakeState& s) {
    if (!s.body.empty()) {
        attron(A_BOLD);
        mvaddch(s.body[0].y, s.body[0].x, getDirectionalHead(s.dir));
        attroff(A_BOLD);
        for (size_t i=1;i<s.body.size();++i) 
            mvaddch(s.body[i].y, s.body[i].x, s.bodyCh);
    }
}

void drawFloatingTexts() {
    for (const auto& ft : floating_texts) {
        if (ft.life > 8) attron(A_BOLD);
        mvprintw(ft.y, ft.x, "%s", ft.text.c_str());
        if (ft.life > 8) attroff(A_BOLD);
    }
}

void drawParticles() {
    attron(A_DIM);
    for (const auto& p : particles) {
        if (p.x >= 1 && p.x < W-1 && p.y >= 1 && p.y < H-1)
            mvaddch((int)p.y, (int)p.x, p.symbol);
    }
    attroff(A_DIM);
}

void drawHUD() {
    mvaddch(HUDY, 0, current_theme.corner_tl);
    for (int i = 1; i < W-1; i++) 
        mvaddch(HUDY, i, current_theme.border_horizontal);
    mvaddch(HUDY, W-1, current_theme.corner_tr);
    
    mvaddch(HUDY+1, 0, current_theme.border_vertical);
    
    attron(A_BOLD);
    mvprintw(HUDY+1, 2, "NIV:%2d", level_);
    attroff(A_BOLD);
    
    mvaddch(HUDY+1, 10, current_theme.separator);
    mvprintw(HUDY+1, 12, "VEL:%3dms", speed_ms);
    
    mvaddch(HUDY+1, 23, current_theme.separator);
    if (time_left <= 5) attron(A_BLINK);
    mvprintw(HUDY+1, 25, "T:%02d", time_left);
    if (time_left <= 5) attroff(A_BLINK);
    
    mvaddch(HUDY+1, 31, current_theme.separator);
    attron(A_BOLD);
    mvprintw(HUDY+1, 33, "P1:%4d", s1.score);
    attroff(A_BOLD);
    
    if (game_mode == 2) {
        mvaddch(HUDY+1, 43, current_theme.separator);
        attron(A_BOLD);
        mvprintw(HUDY+1, 45, "P2:%4d", s2.score);
        attroff(A_BOLD);
    }
    
    mvaddch(HUDY+1, W-1, current_theme.border_vertical);
    
    mvaddch(HUDY+2, 0, current_theme.corner_bl);
    for (int i = 1; i < W-1; i++) 
        mvaddch(HUDY+2, i, current_theme.border_horizontal);
    mvaddch(HUDY+2, W-1, current_theme.corner_br);
    
    mvprintw(HUDY+3, 2, "Tema: %s", current_theme.name.c_str());
    mvprintw(HUDY+3, 30, "[P]Pausa [T]Tema");
    
    if (combo_p1 >= 3) {
        attron(A_BOLD | A_BLINK);
        mvprintw(HUDY+4, 2, "COMBO x%d!", combo_p1);
        attroff(A_BOLD | A_BLINK);
    }
}

void clearCell(int x, int y) { mvaddch(y, x, ' '); }

// ==================================================
// MOVIMIENTO SERPIENTE
// ==================================================
void eraseSnakeTail(const SnakeState& s) {
    if (!s.body.empty()) clearCell(s.body.back().x, s.body.back().y);
}

void advanceSnake(SnakeState& s) {
    if (!s.alive || s.dir==-1 || s.body.empty()) return;

    Coord head = s.body[0];
    switch (s.dir) { 
        case 0: head.y--; break; 
        case 1: head.y++; break;
        case 2: head.x--; break; 
        case 3: head.x++; break; 
    }

    if (!insideField(head.x, head.y) || onTrap(head.x, head.y)) { 
        s.lives--;
        if (s.lives <= 0) {
            s.alive = false;
            showDeathAnimation(head.x, head.y);
        } else {
            showFloatingText("-1 VIDA", head.x, head.y);
            s.body.clear();
            s.body = {{W/2, H/2}};
            s.dir = 3;
        }
        return; 
    }
    
    for (const auto& c: s.body) 
        if (head.x==c.x && head.y==c.y) { 
            s.alive=false; 
            showDeathAnimation(head.x, head.y);
            return; 
        }
    
    SnakeState& other = (s.id==1 ? s2 : s1);
    for (const auto& c: other.body) 
        if (head.x==c.x && head.y==c.y) { 
            s.alive=false; 
            showDeathAnimation(head.x, head.y);
            return; 
        }

    bool ate = (head.x==food.x && head.y==food.y);
    PowerUp* pu = nullptr;
    bool gotPowerUp = onPowerUp(head.x, head.y, pu);
    
    if (!ate) eraseSnakeTail(s);

    for (int i=(int)s.body.size()-1;i>0;--i) s.body[i]=s.body[i-1];
    s.body[0] = head;

    if (ate) {
        int points = 7 + (combo_p1 >= 3 ? combo_p1 * 2 : 0);
        s.score += points;
        combo_p1++;
        s.body.push_back(s.body.back());
        showFloatingText("+" + to_string(points), head.x, head.y-1);
        createParticles(food.x, food.y, 8);
        placeFood();
        sem_post(&sem_food_ready);
    }
    
    if (gotPowerUp && pu) {
        pu->active = false;
        switch(pu->type) {
            case 0: 
                showFloatingText("SPEED!", head.x, head.y);
                speed_ms = max(50, speed_ms - 30);
                break;
            case 1: 
                showFloatingText("SHIELD!", head.x, head.y);
                break;
            case 2: 
                showFloatingText("2X SCORE!", head.x, head.y);
                s.score += 20;
                break;
            case 3: 
                if (s.lives < 5) {
                    s.lives++;
                    showFloatingText("+VIDA", head.x, head.y);
                }
                break;
        }
        createParticles(pu->pos.x, pu->pos.y, 12);
    }
}

// ==================================================
// MENÚS Y PANTALLAS
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
    mvprintw(20, 22, "4. Cambiar tema visual");
    mvprintw(21, 22, "5. Puntajes destacados");
    mvprintw(22, 22, "6. Instrucciones");
    mvprintw(23, 22, "7. Salir");

    refresh();
    pthread_mutex_unlock(&mtx_scr);
}

void showInstructions() {
    pthread_mutex_lock(&mtx_scr);
    clear();
    
    attron(A_BOLD);
    mvprintw(1, 20, "╔════════════════════════════╗");
    mvprintw(2, 20, "║   INSTRUCCIONES DEL JUEGO  ║");
    mvprintw(3, 20, "╚════════════════════════════╝");
    attroff(A_BOLD);
    
    attron(A_BOLD);
    mvprintw(5, 5, "OBJETIVO:");
    attroff(A_BOLD);
    mvprintw(6, 5, "Controla tu serpiente, come comida y sobrevive.");
    mvprintw(7, 5, "¡Evita las trampas y los bordes!");
    
    attron(A_BOLD);
    mvprintw(9, 5, "CONTROLES:");
    attroff(A_BOLD);
    mvprintw(10, 8, "Jugador 1: W(↑) S(↓) A(←) D(→)");
    mvprintw(11, 8, "Jugador 2: Flechas del teclado");
    mvprintw(12, 8, "P - Pausar juego");
    mvprintw(13, 8, "T - Cambiar tema visual");
    
    attron(A_BOLD);
    mvprintw(15, 5, "ELEMENTOS:");
    attroff(A_BOLD);
    mvprintw(16, 8, "^v<> - Serpiente (cabeza muestra dirección)");
    mvprintw(17, 8, "@    - Comida (+7 puntos)");
    mvprintw(18, 8, "X    - Trampa (pierdes 1 vida)");
    mvprintw(19, 8, "S    - Speed Boost");
    mvprintw(20, 8, "$    - Puntos x2");
    mvprintw(21, 8, "H    - Vida extra");
    
    attron(A_BOLD);
    mvprintw(23, 5, "NIVELES:");
    attroff(A_BOLD);
    mvprintw(24, 8, "Cada %d segundos subes de nivel", TIMER_START);
    mvprintw(25, 8, "Aumenta velocidad y número de trampas");
    
    mvprintw(27, 15, "Presiona cualquier tecla para continuar...");
    
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

void showThemeSelector() {
    pthread_mutex_lock(&mtx_scr);
    clear();
    
    attron(A_BOLD);
    mvprintw(2, 20, "SELECCIONA UN TEMA VISUAL");
    attroff(A_BOLD);
    
    for (size_t i = 0; i < available_themes.size(); i++) {
        if (i == theme_index) attron(A_REVERSE | A_BOLD);
        
        mvprintw(5 + i*2, 15, "%zu. %s", i+1, available_themes[i].name.c_str());
        mvprintw(5 + i*2, 40, "Preview: ");
        addch(available_themes[i].p1_head);
        addch(available_themes[i].p1_body);
        mvprintw(5 + i*2, 50, "%c", available_themes[i].food_basic);
        mvprintw(5 + i*2, 53, "%c", available_themes[i].trap);
        
        if (i == theme_index) attroff(A_REVERSE | A_BOLD);
    }
    
    mvprintw(20, 15, "Flechas para navegar, ENTER para seleccionar");
    refresh();
    
    bool selecting = true;
    while (selecting) {
        int ch = getch();
        if (ch == KEY_UP && theme_index > 0) {
            theme_index--;
            showThemeSelector();
        }
        else if (ch == KEY_DOWN && theme_index < (int)available_themes.size() - 1) {
            theme_index++;
            showThemeSelector();
        }
        else if (ch == '\n' || ch == '\r') {
            current_theme = available_themes[theme_index];
            applyThemeToSnakes();
            selecting = false;
        }
        else if (ch == 27) selecting = false;
    }
    
    pthread_mutex_unlock(&mtx_scr);
}

vector<ScoreRecord> loadScores() {
    vector<ScoreRecord> records;
    ifstream f("scores.txt");
    if (!f) return records;
    
    string line;
    while (getline(f, line)) {
        ScoreRecord rec;
        size_t pos1 = line.find("lvl:");
        size_t pos2 = line.find("p1:");
        size_t pos3 = line.find("p2:");
        size_t pos4 = line.find("|", pos3);
        
        if (pos1 != string::npos && pos2 != string::npos) {
            rec.mode = line.substr(0, pos1-3);
            rec.level = atoi(line.substr(pos1+4, pos2-pos1-7).c_str());
            rec.p1_score = atoi(line.substr(pos2+3, pos3-pos2-5).c_str());
            if (pos3 != string::npos)
                rec.p2_score = atoi(line.substr(pos3+3, pos4-pos3-3).c_str());
            rec.date = line.substr(pos4+2);
            records.push_back(rec);
        }
    }
    return records;
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
    
    attron(A_BOLD);
    mvprintw(1, 15, "╔═══════════════════════════════════╗");
    mvprintw(2, 15, "║   PUNTAJES DESTACADOS - TOP 10   ║");
    mvprintw(3, 15, "╚═══════════════════════════════════╝");
    attroff(A_BOLD);
    
    vector<ScoreRecord> records = loadScores();
    
    if (records.empty()) {
        mvprintw(6, 20, "No hay puntajes registrados aún");
        mvprintw(8, 20, "¡Sé el primero en jugar!");
    } else {
        sort(records.begin(), records.end(), 
             [](const ScoreRecord& a, const ScoreRecord& b) {
                 return a.p1_score > b.p1_score;
             });
        
        mvprintw(5, 2, "POS  MODO      NIVEL  P1      P2      FECHA");
        mvprintw(6, 2, "════════════════════════════════════════════════════");
        
        int limit = min(10, (int)records.size());
        for (int i = 0; i < limit; ++i) {
            string display_mode = (records[i].mode.find("1P") != string::npos) ? "1P" : "2P";
            mvprintw(7 + i, 2, "%2d.  %-8s  %3d    %4d    %4d    %s", 
                     i + 1,
                     display_mode.c_str(),
                     records[i].level,
                     records[i].p1_score,
                     records[i].p2_score,
                     records[i].date.substr(0, 10).c_str());
        }
    }
    
    mvprintw(20, 2, "════════════════════════════════════════════════════");
    mvprintw(21, 20, "Presiona cualquier tecla...");
    refresh();
    getch();
    pthread_mutex_unlock(&mtx_scr);
}

bool gameOverScreen() {
    pthread_mutex_lock(&mtx_scr);
    clear();

    mvprintw(2, 5, "  __ _  __ _ _ __ ___   ___    _____   _____ _ __  ");
    mvprintw(3, 5, " / _` |/ _` | '_ ` _ \\ / _ \\  / _ \\ \\ / / _ \\ '__| ");
    mvprintw(4, 5, "| (_| | (_| | | | | | |  __/ | (_) \\ V /  __/ |    ");
    mvprintw(5, 5, " \\__, |\\__,_|_| |_| |_|\\___|  \\___/ \\_/ \\___|_|    ");
    mvprintw(6, 5, " |___/                                              ");

    mvprintw(9, 10, "Resultados finales:");
    mvprintw(11, 12, "Jugador 1: %d puntos (Vidas: %d)", s1.score, s1.lives);
    if (game_mode == 2)
        mvprintw(12, 12, "Jugador 2: %d puntos (Vidas: %d)", s2.score, s2.lives);
    mvprintw(14, 12, "Nivel alcanzado: %d", level_);

    mvprintw(17, 10, "¿Qué deseas hacer?");
    mvprintw(19, 12, "1. Volver al menú");
    mvprintw(20, 12, "2. Salir");

    refresh();

    int ch;
    do {
        ch = getch();
    } while (ch != '1' && ch != '2');

    pthread_mutex_unlock(&mtx_scr);
    return (ch == '1');
}

void showLoadingScreen(const char* message) {
    pthread_mutex_lock(&mtx_scr);
    clear();
    
    int centerY = H/2;
    int centerX = W/2;
    
    mvprintw(centerY-2, centerX-10, "%s", message);
    
    for (int i = 0; i < 20; i++) {
        mvaddch(centerY, centerX-10+i, '.');
    }
    
    const char* loading_chars = "|/-\\";
    for (int progress = 0; progress <= 20; progress++) {
        mvaddch(centerY, centerX-10+progress, '=');
        mvaddch(centerY, centerX+12, loading_chars[progress % 4]);
        refresh();
        usleep(50000);
    }
    
    attron(A_BOLD);
    mvprintw(centerY+2, centerX-5, "LISTO!");
    attroff(A_BOLD);
    refresh();
    usleep(500000);
    
    pthread_mutex_unlock(&mtx_scr);
}

// ==================================================
// HILOS PRINCIPALES (10 FUNCIONES void*)
// ==================================================

// HILO 1: Serpiente P1
void* th_snake_p1(void* arg) {
    while (true) {
        pthread_mutex_lock(&mtx_state);
        if (!game_running) { pthread_mutex_unlock(&mtx_state); break; }
        if (!game_paused && !show_next_level) advanceSnake(s1);
        pthread_mutex_unlock(&mtx_state);
        usleep(speed_ms * 1000);
    }
    return nullptr;
}

// HILO 2: Serpiente P2
void* th_snake_p2(void* arg) {
    while (true) {
        pthread_mutex_lock(&mtx_state);
        if (!game_running) { pthread_mutex_unlock(&mtx_state); break; }
        if (game_mode == 2 && !game_paused && !show_next_level) advanceSnake(s2);
        pthread_mutex_unlock(&mtx_state);
        usleep(speed_ms * 1000);
    }
    return nullptr;
}

// HILO 3: Entrada de teclado
void* th_input(void* arg) {
    nodelay(stdscr, TRUE);
    while (true) {
        pthread_mutex_lock(&mtx_state); 
        bool running = game_running;
        bool showNL = show_next_level; 
        pthread_mutex_unlock(&mtx_state);
        
        if (!running) break;
        if (showNL) { usleep(20*1000); continue; }
        
        pthread_mutex_lock(&mtx_scr); 
        int ch = getch(); 
        pthread_mutex_unlock(&mtx_scr);
        
        if (ch != ERR) {
            pthread_mutex_lock(&mtx_state);
            
            if (ch == 'p' || ch == 'P') {
                game_paused = !game_paused;
                pthread_mutex_unlock(&mtx_state);
                
                if (game_paused) {
                    pthread_mutex_lock(&mtx_scr);
                    attron(A_BOLD | A_REVERSE);
                    mvprintw(H/2 - 1, W/2 - 8, "                ");
                    mvprintw(H/2,     W/2 - 8, "  JUEGO PAUSADO ");
                    mvprintw(H/2 + 1, W/2 - 8, "  Presiona P    ");
                    mvprintw(H/2 + 2, W/2 - 8, "                ");
                    attroff(A_BOLD | A_REVERSE);
                    refresh();
                    pthread_mutex_unlock(&mtx_scr);
                }
                continue;
            }
            
            if (ch == 't' || ch == 'T') {
                bool was_paused = game_paused;
                game_paused = true;
                pthread_mutex_unlock(&mtx_state);
                
                showThemeSelector();
                
                pthread_mutex_lock(&mtx_state);
                game_paused = was_paused;
                pthread_mutex_unlock(&mtx_state);
                continue;
            }
            
            if (game_paused) {
                pthread_mutex_unlock(&mtx_state);
                usleep(20 * 1000);
                continue;
            }
            
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

// HILO 4: Renderizado
void* th_render(void* arg) {
    while (true) {
        pthread_mutex_lock(&mtx_state);
        if (!game_running) { pthread_mutex_unlock(&mtx_state); break; }
        bool paused = game_paused;
        bool showNL = show_next_level;
        pthread_mutex_unlock(&mtx_state);
        
        pthread_mutex_lock(&mtx_scr);
        clear();
        
        if (showNL) {
            attron(A_BOLD);
            mvprintw(H/2-1, (W/2)-5, "=== NEXT LEVEL ===");
            mvprintw(H/2+1, (W/2)-7, "Entrando al nivel %d...", level_);
            attroff(A_BOLD);
        } else {
            drawBorders(); 
            drawFood(); 
            drawTraps();
            drawPowerUps();
            drawSnake(s1); 
            if (game_mode==2) drawSnake(s2);
            drawFloatingTexts();
            drawParticles();
            drawHUD();
        }
        
        refresh();
        pthread_mutex_unlock(&mtx_scr);
        
        usleep(50 * 1000);
    }
    return nullptr;
}

// HILO 5: Animación de comida
void* th_food_animator(void* arg) {
    while (true) {
        pthread_mutex_lock(&mtx_state);
        if (!game_running) { pthread_mutex_unlock(&mtx_state); break; }
        pthread_mutex_unlock(&mtx_state);
        
        food_animation_frame = (food_animation_frame + 1) % 4;
        usleep(200000);
    }
    return nullptr;
}

// HILO 6: Spawner de comida (productor con semáforo)
void* th_food_spawner(void* arg) {
    int ticks = 0;
    while (true) {
        pthread_mutex_lock(&mtx_state);
        if (!game_running) { pthread_mutex_unlock(&mtx_state); break; }
        pthread_mutex_unlock(&mtx_state);
        
        if (++ticks >= 80) {
            pthread_mutex_lock(&mtx_state);
            placeFood();
            pthread_mutex_unlock(&mtx_state);
            ticks = 0;
        }
        usleep(100 * 1000);
    }
    return nullptr;
}

// HILO 7: Temporizador
void* th_timer(void* arg) {
    int counterNL = 0;
    while (true) {
        sleep(1);
        pthread_mutex_lock(&mtx_state);
        if (!game_running) { pthread_mutex_unlock(&mtx_state); break; }
        
        if (game_paused) {
            pthread_mutex_unlock(&mtx_state);
            continue;
        }
        
        if (show_next_level) {
            counterNL++;
            if (counterNL >= 2) {
                show_next_level = false;
                counterNL = 0;
                sem_post(&sem_level_change);
            }
            pthread_mutex_unlock(&mtx_state);
            continue;
        }
        
        time_left--;
        if (time_left <= 0) {
            level_++;
            if (speed_ms > 60) speed_ms -= 10;
            time_left = TIMER_START;
            show_next_level = true;
        }
        pthread_mutex_unlock(&mtx_state);
    }
    return nullptr;
}

// HILO 8: Regeneración de trampas
void* th_traps_regen(void* arg) {
    while (true) {
        pthread_mutex_lock(&mtx_state);
        if (!game_running) { pthread_mutex_unlock(&mtx_state); break; }
        
        bool showNL = show_next_level;
        int desired = maxTrapsForLevel();
        int current = (int)traps.size();
        pthread_mutex_unlock(&mtx_state);

        if (!showNL && current < desired) {
            pthread_mutex_lock(&mtx_state);
            addTrap();
            pthread_mutex_unlock(&mtx_state);
            sleep(2);
        } else {
            sleep(1);
        }
    }
    return nullptr;
}

// HILO 9: Spawner de power-ups
void* th_powerup_spawner(void* arg) {
    while (true) {
        pthread_mutex_lock(&mtx_state);
        if (!game_running) { pthread_mutex_unlock(&mtx_state); break; }
        pthread_mutex_unlock(&mtx_state);
        
        sleep(15);
        
        pthread_mutex_lock(&mtx_state);
        if (powerups.size() < 2) {
            spawnPowerUp();
        }
        pthread_mutex_unlock(&mtx_state);
    }
    return nullptr;
}

// HILO 10: Efectos visuales (partículas, textos flotantes)
void* th_visual_effects(void* arg) {
    while (true) {
        pthread_mutex_lock(&mtx_state);
        if (!game_running) { pthread_mutex_unlock(&mtx_state); break; }
        
        // Actualizar textos flotantes
        for (auto it = floating_texts.begin(); it != floating_texts.end();) {
            it->y--;
            it->life--;
            if (it->life <= 0) {
                it = floating_texts.erase(it);
            } else {
                ++it;
            }
        }
        
        // Actualizar partículas
        for (auto it = particles.begin(); it != particles.end();) {
            it->x += it->vx;
            it->y += it->vy;
            it->life--;
            
            if (it->life <= 0 || it->x < 1 || it->x >= W-1 || it->y < 1 || it->y >= H-1) {
                it = particles.erase(it);
            } else {
                ++it;
            }
        }
        
        // Actualizar power-ups
        for (auto& pu : powerups) {
            if (pu.active) {
                pu.duration--;
                if (pu.duration <= 0) pu.active = false;
            }
        }
        
        // Decrementar combo si no se come en 3 segundos
        static int combo_timer = 0;
        combo_timer++;
        if (combo_timer >= 30) {
            combo_p1 = max(0, combo_p1 - 1);
            combo_p2 = max(0, combo_p2 - 1);
            combo_timer = 0;
        }
        
        pthread_mutex_unlock(&mtx_state);
        usleep(100000);
    }
    return nullptr;
}

// ==================================================
// INICIALIZACIÓN Y LIMPIEZA
// ==================================================
void init_synchronization() {
    sem_init(&sem_food_ready, 0, 0);
    sem_init(&sem_level_change, 0, 0);
}

void destroy_synchronization() {
    sem_destroy(&sem_food_ready);
    sem_destroy(&sem_level_change);
    pthread_mutex_destroy(&mtx_state);
    pthread_mutex_destroy(&mtx_scr);
    pthread_cond_destroy(&cond_game_event);
}

void startGame(int mode) {
    game_mode = mode;
    resetGame();
    
    init_synchronization();
    
    pthread_t threads[10];
    pthread_create(&threads[0], nullptr, th_snake_p1, nullptr);
    pthread_create(&threads[1], nullptr, th_snake_p2, nullptr);
    pthread_create(&threads[2], nullptr, th_input, nullptr);
    pthread_create(&threads[3], nullptr, th_render, nullptr);
    pthread_create(&threads[4], nullptr, th_food_animator, nullptr);
    pthread_create(&threads[5], nullptr, th_food_spawner, nullptr);
    pthread_create(&threads[6], nullptr, th_timer, nullptr);
    pthread_create(&threads[7], nullptr, th_traps_regen, nullptr);
    pthread_create(&threads[8], nullptr, th_powerup_spawner, nullptr);
    pthread_create(&threads[9], nullptr, th_visual_effects, nullptr);
    
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
    
    for (int i = 0; i < 10; i++) {
        pthread_join(threads[i], nullptr);
    }
    
    destroy_synchronization();
    
    saveScore(mode==1 ? string("Snake 1P") : string("Snake 2P"));
    bool backToMenu = gameOverScreen(); 
    if (!backToMenu) quit_program = true;
}

// ==================================================
// NCURSES INIT/FIN Y MAIN
// ==================================================
void init_ncurses() { 
    initscr(); 
    cbreak(); 
    noecho(); 
    keypad(stdscr, TRUE); 
    nodelay(stdscr, FALSE); 
    curs_set(0); 
    srand(time(nullptr)); 
}

void end_ncurses() { 
    endwin(); 
}

int main() {
    init_ncurses();
    
    showLoadingScreen("Cargando Snake Game");
    applyThemeToSnakes();
    showInstructions();

    while (!quit_program) {
        pthread_mutex_lock(&mtx_scr);
        nodelay(stdscr, FALSE);
        flushinp();
        pthread_mutex_unlock(&mtx_scr);

        showMenu();

        int op;
        do {
            op = getch();
        } while (op != '1' && op != '2' && op != '3' && op != '4' && 
                 op != '5' && op != '6' && op != '7');

        if (op=='1') startGame(1);
        else if (op=='2') startGame(2);
        else if (op=='3') showDifficulty();
        else if (op=='4') showThemeSelector();
        else if (op=='5') showScores();
        else if (op=='6') showInstructions();
        else if (op=='7') quit_program = true;
    }

    end_ncurses();
    return 0;
}