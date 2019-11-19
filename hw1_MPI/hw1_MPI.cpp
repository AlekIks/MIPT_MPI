#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <cstdlib>
#include <fstream>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>

////////////////////////////////////
//// Команды

// NOCOMMAND нужна для корректой работы map
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
    Order(COMMANDS command, uint32_t steps) : command(command), new_steps(steps) {};
    COMMANDS command = NOCOMMAND;
    uint32_t new_steps = 0;

};

///////////////////////////////////
/// Переход по правилам игры

char next_step_cell(char* TABLE, int N, int M, int pos) {
    char table [N][M];
    for (uint32_t i = 0; i < N; i ++) {
        for (uint32_t j = 0; j < M; j ++) {
            table[i][j] = *(TABLE + i * N + j);
        }
    }
    int j = pos % M;
    int i = (pos - j) / N;

    i += N * M;
    j += N * M;
    int sum = table[(i - 1) % M][(j - 1) % N] +
              table[(i - 1) % M][j % N] +
              table[(i - 1) % M][(j + 1) % N] +
              table[i % M][(j - 1) % N] +
              table[i % M][(j + 1) % N] +
              table[(i + 1) % M][(j - 1) % N] +
              table[(i + 1) % M][j % N] +
              table[(i + 1) % M][(j + 1) % N] -
              '0' * 8;

    // клетка оживает, если рядом три живых соседа
    if (*(TABLE + pos) == '0' && sum == 3)
        return '1';
    // клетка остается живой, если рядом 2 или три живых соседа
    if (*(TABLE + pos) == '1' && (sum == 2 || sum == 3))
        return '1';
    return '0';
}

//////////////////////////////////////1


int main(int argc, char* argv[]) {

    ////////////////////////////////////
    //// Текущая директория, достаем HELP-файл
    const std::string argv_str(argv[0]);
    const std::string cur_dir = argv_str.substr(0, argv_str.find_last_of("/"));
    const std::string help_file_name = cur_dir + "/../taskLife.txt";

    ////////////////////////////////////
    //// Параметры таблицы
    char* TABLE = nullptr;
    uint32_t N = 0, M = 0;
    bool is_game_started = false, is_game_running = false;   // флаги состояния игры
    std::map <std::string, uint8_t> used_configurations;     // чтобы отслеживать зацикливание
    uint32_t cur_step = 0;                                   // текущий шаг

    ////////////////////////////////////
    //// Обработка команд
    std::string cmd;
    while(std::cin >> cmd) {
        switch(commands[cmd]){
            /***
             * START
             * @param: количество строк поля N, количество столбцов M,
             * способ инициализации init_status: random или csv
             * @result: готовое к игре поле TABLE
             */
            case START: {
                std::cin >> N >> M;

                std::string init_status;
                if (!(std::cin >> init_status)) {
                    perror("Не указан способ инициализации поля.");
                    break;
                }
                TABLE = new char[N * M];

                switch (commands[init_status]) {
                    case RANDOM: {                                      // Случайное заполнение таблицы
                        for (uint32_t i = 0; i < N * M; i++) {
                            TABLE[i] = rand() % 2 + '0';
                        }
                        used_configurations[TABLE] = 1;                 // Отметили стартовую конфигурацию
                        is_game_started = true;
                        break;
                    }
                    case CSV: {                                         // Чтение готового поля из CSV
                        char file_name[PATH_MAX];
                        scanf("%s", file_name);
                        int csv_fd = open(file_name, O_RDONLY);

                        if (csv_fd == -1) {
                            perror("Файл не доступен, попробуйте еще раз");
                            continue;
                        }
                        uint32_t i = 0;
                        char a;
                        while(read(csv_fd, &a, 1)) {
                            if (a == '0' || a == '1') {
                                std::cout << a << " ";
                                TABLE[i] = a;
                                i ++;
                            }
                            if(i >= M * N) {
                                perror("Проверьте переданный файл и размеры таблицы.");
                                break;
                            }
                        }
                        close(csv_fd);

                        if (i != M * N) {
                            perror("В таблице не хватает элементов.");
                            break;
                        }
                        used_configurations[TABLE] = 1;                 // Отметили стартовую конфигурацию
                        is_game_started = true;
                        break;
                    }
                    default:
                        perror("Неверный способ инициализации поля.");
                        break;
                }
                break;
            }
            /***
             * RUN
             * @param: количество шагов
             * @result: запуск вычисления
             */
            case RUN: {
                if (!is_game_started) {                     // Если игра не началась - надо начать
                    perror("Сначала начните игру.");
                    break;
                }

                uint32_t steps_num = 0;
                std::cin >> steps_num;

                // Если игра уже идет, то докидиваем новые шаги подмастерью
                if (is_game_running) {
                    Order add_steps_order (
                            ADD_STEPS,
                            steps_num);
                    break;
                }

                // Если не идет, то запускаем
                is_game_running = true;

                // Инициализация MPI
                if (MPI_Init (NULL, NULL) != MPI_SUCCESS) {
                    perror("Ошибка инициализации MPI ");
                    return 1;
                }

                int world_size, world_id;
                MPI_Comm_size(MPI_COMM_WORLD, &world_size);	// кол-во процессов
                MPI_Comm_rank(MPI_COMM_WORLD, &world_id);	// локальный id

                if (world_size <= 1) {
                    perror("Игра может работать только на двух и более процессах.");
                    return 1;
                }

                // Информация о построчном разделении игровой таблицы
                const uint32_t num_workers = world_size - 2;	// кол-во "работников"
                const uint32_t num_for_craftsman = N % num_workers;	// избыток посчитает "подмастерье"
                const uint32_t num_elem_per_worker = (N - num_for_craftsman) / num_workers;

                switch(world_id) {
                    // "Мастер" пойдет дальше принимать команды
                    case 0: {
                        break;
                    }
                    // "Подмастерье"
                    case 1: {
                        while(true) {
                            // рассылает подстроки
                            for (int i = 0; i < num_workers; i ++) {
                                if (MPI_Send(
                                        TABLE + i * (num_elem_per_worker) * M - M, // TABLE + offset - одна read-only строка
                                        num_elem_per_worker * M + 2 * M,   // кол-во элементов на "работника" + 2 r-o строки
                                        MPI_CHAR,		// тип
                                        i + 2,			// кому шлём
                                        0,			// тэг
                                        MPI_COMM_WORLD		// группа
                                )  != MPI_SUCCESS) {
                                    perror("Ошибка передачи работнику\n");
                                }
                            }

                            // считает свою часть
                            char* next_step_TABLE = new char [N * M];
                            for (uint32_t i = num_workers * num_elem_per_worker * M; i < M * N; i ++) {
                                next_step_TABLE[i] = next_step_cell(TABLE, N, M, i);
                            }

                            // принимает куски таблицы от работников, сливает в одну
                            for (int i = 0; i < num_workers; i ++) {
                                if (MPI_Recv(
                                        next_step_TABLE + i * (num_elem_per_worker) * M, 	// куда пишем
                                        num_elem_per_worker,			// кол-во элементов
                                        MPI_CHAR, 		// тип
                                        i + 2,		// от кого
                                        0,			// тэг?
                                        MPI_COMM_WORLD,	// группа
                                        MPI_STATUS_IGNORE	// флаг
                                ) != MPI_SUCCESS) {
                                    perror("Ошибка получения от работника\n");
                                }
                            }

                            // обновляем данные
                            delete [] TABLE;
                            TABLE = next_step_TABLE;
                            cur_step ++;
                            /*
                             * Todo: если еще есть шаги, принимаем IRecv Order от мастера
                             * Todo: MPI Test, ничего не прилетело - продолжаем вычислять
                             * Todo: если шагов не осталось - повиснуть на MPI Wait, ждать Order
                             * todo: обработчики разных приказов
                             */
                        }
                    }
                }
                break;
            }
            /***
             * STOP
             * @return: номер текущего шага,
             * текущее состояние поля:
             * "Вы прервали вычисление, следующий шаг не запустится." или
             * "Вычислений не были запущены."
             */
            case STOP: {
                break;
            }
            case STATUS: {
                /*
                 * todo: отправлять Order подмастерью
                 */
                if (!is_game_started) {
                    std::cout << "Игра еще не началась." << "\n";
                }
                if (!is_game_running) {
                    for (uint32_t i = 0; i < N; i++) {
                        for (uint32_t j = 0; j < M; j++) {
                            std::cout << TABLE[i * N + j] << " ";
                        }
                        std::cout << "\n";
                    }
                }
                break;
            }
            case RESET: {
                break;
            }
            case QUIT: {
                break;
            }
            /***
             * Печатает ТЗ
             */
            case HELP: {
                std::ifstream helpfile(help_file_name, std::ios::out);
                if (!helpfile.is_open()) {
                    perror("Помощи не будет, мне жаль (нет).");
                    continue;
                }
                char a;
                while(helpfile.read(&a, 1)) {
                    std::cout << a;
                }
                helpfile.close();
                break;
            }
            default: {
                perror("Такого я делать не умею. В любой непонятной ситуации смотрите HELP.");
                break;
            }
        }
    }
    return 0;
}

