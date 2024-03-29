#ifndef PDJUST_CLIFEGAME_H
#define PDJUST_CLIFEGAME_H

#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <cstdlib>
#include <fstream>
#include <fcntl.h>
#include <limits.h>
#include <mpi.h>
#include <unistd.h>

////////////////////////////////////
//// Команды

enum COMMANDS {NOCOMMAND, START, RUN, STOP, STATUS, RESET, QUIT, HELP, RANDOM, CSV,
    ADD_STEPS};
std::map <std::string, COMMANDS> commands = {
        {"START", START},
        {"RUN", RUN},
        {"STOP", STOP},
        {"STATUS", STATUS},
        {"RESET", RESET},
        {"QUIT", QUIT},
        {"HELP", HELP},
        {"random", RANDOM},
        {"csv", CSV}
};

///////////////////////////////////
/// Мастер будет отдавать подмастерью приказы

struct Order {
    Order(COMMANDS command = NOCOMMAND, uint32_t steps = 0, uint32_t fs = 0) :
            command(command),
            new_steps(steps),
            finished_steps(fs){};
    COMMANDS command = NOCOMMAND;
    uint32_t new_steps = 0;
    uint32_t finished_steps = 0;
};

////////////////////////////////////

class CLifeGame{
public:
    static CLifeGame* get_instance() {
        if (!instance) {
            instance = new CLifeGame();
        }
        return instance;
    }

    void Start(uint32_t N, uint32_t M, const std::string& init_status);
    void Run(uint32_t steps_num);
    void Status();
    void Reset(COMMANDS cmd);
    void Stop(COMMANDS cmd);
    void Quit();
private:
    static CLifeGame* instance;

    ////////////////////////////////////
    /// Параметры таблицы

    char* TABLE = nullptr;
    uint32_t N = 0, M = 0;
    // флаги состояния игры
    bool is_game_started = false;
    bool is_game_running = false;
    bool is_game_quit = false;

    ////////////////////////////////////
    /// MPI

    int world_size, world_id;

    ////////////////////////////////////
    /// Конструктор

    CLifeGame();
};


#endif
