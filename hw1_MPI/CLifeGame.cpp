
#include "CLifeGame.h"

CLifeGame* CLifeGame::instance = nullptr;

///////////////////////////////////
/// Переход по правилам игры

char next_step_cell(const char* TABLE, int N, int M, int pos) {
    char table [N][M];
    for (uint32_t i = 0; i < N; i ++) {
        for (uint32_t j = 0; j < M; j ++) {
            table[i][j] = *(TABLE + i * M + j);
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

///////////////////////////////////////////////////////////

/***
 * Конструктор
 */

CLifeGame::CLifeGame() {
    MPI_Init(NULL, NULL);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);	// кол-во процессов
    MPI_Comm_rank(MPI_COMM_WORLD, &world_id);	// локальный id

    if (world_size <= 1) {
        MPI_Finalize();
        perror("Игра может работать только на двух и более процессах.");
    }

}


/***
* START
* @param: количество строк поля N, количество столбцов M,
* способ инициализации init_status: random или csv
* @result: готовое к игре поле TABLE
*/
void CLifeGame::Start(uint32_t n, uint32_t m, const std::string& init_status) {
    if (is_game_quit) {
        std::cout << "Игра уже завершилась\n";
        return;
    }
    if (is_game_started && !world_id) {
        std::cout << "Игра уже была инициализирована.\n";
    }

    // это общие для всех параметры
    N = n, M = m;
    is_game_started = true;
    // полная таблица хранится только у
    // мастера и подмастерья
    if (world_id == 0 || world_id == 1) {
        TABLE = new char[N * M];
    }

    if (world_id) {             // обрабатывает команды мастер
        return;
    }

    switch (commands[init_status]) {
        case RANDOM: {                                      // Случайное заполнение таблицы
            for (uint32_t i = 0; i < N * M; i++) {
                TABLE[i] = rand() % 2 + '0';
            }
            is_game_started = true;
            break;
        }
        case CSV: {                                         // Чтение готового поля из CSV
            char file_name[PATH_MAX];
            scanf("%s", file_name);
            int csv_fd = open(file_name, O_RDONLY);

            if (csv_fd == -1) {
                std::cout << "Файл не доступен, попробуйте еще раз\n";
                return;
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
                    std::cout << "Проверьте переданный файл и размеры таблицы.\n";
                    return;
                }
            }
            close(csv_fd);

            if (i != M * N) {
                std::cout << "В таблице не хватает элементов.\n";
                return;
            }
            return;
        }
        default: {
            std::cout << "Неверный способ инициализации поля.\n";
            return;
        }
    }
}

/***
* RUN
* @param: количество шагов
* @result: запуск вычисления
*/
void CLifeGame::Run(uint32_t steps_num) {
    if (is_game_quit) {
        return;
    }
    // Мастер обрабатывает команду
    if (!world_id) {
        if (!is_game_started && !world_id) {                     // Если игра не началась - надо начать
            std::cout << "Сначала начните игру.";
            return;
        }

        // Если игра уже идет, то докидиваем новые шаги подмастерью
        if (is_game_running) {
            Order add_steps_order (
                    ADD_STEPS,
                    steps_num);
            MPI_Request req;
            MPI_Isend(
                    &add_steps_order,     // шлем приказ
                    sizeof(Order),
                    MPI_CHAR,
                    1,              // подмастерью
                    0,
                    MPI_COMM_WORLD,
                    &req
            );
            return;
        }

        // Если не идет, то запускаем
        is_game_running = true;
        // Отправляем подмастерью таблицу
        if (MPI_Send(
                TABLE,
                N * M,
                MPI_CHAR,
                1,
                0,
                MPI_COMM_WORLD
        ) != MPI_SUCCESS) {
            perror("Ошибка первой отправки таблицы");
        }
        return;
    }

    // Информация о построчном разделении игровой таблицы
    // Основные идеи:
    // 1. Подмастерье сам считает ход для первой и последней
    // строк и для остатка.
    // 2. Работники получают по две строки, из которых могут
    // только читать, и еще num_elem_per_worker строк, для
    // которых вернут значение следующего хода
    const uint32_t num_workers = world_size - 2;	// кол-во "работников"
    const uint32_t num_for_submaster = (N - 2) % num_workers;	// избыток для "подмастерья"
    const uint32_t num_elem_per_worker = (N - 2 - num_for_submaster) / num_workers;

    switch(world_id) {
        ////////////////////////////////////////////////////////////////
        ///////// "Подмастерье"
        case 1: {
            int cur_step = 0;
            int steps_to_do = steps_num;  // оставшиеся шаги
            Order master_order; // приказы мастера

            // получаем таблицу
            if (MPI_Recv(
                    TABLE,
                    N * M,
                    MPI_CHAR,
                    0,
                    0,
                    MPI_COMM_WORLD,
                    MPI_STATUS_IGNORE) != MPI_SUCCESS) {
                perror("Ошибка первого получения таблицы.");
            }

            // запираем подмастерье
            while(true) {
                if (steps_to_do > 0) {
                    // рассылает подстроки
                    for (int i = 0; i < num_workers; i ++) {
                        if (MPI_Send(
                                TABLE + i * (num_elem_per_worker) * M, // TABLE + offset - одна read-only строка
                                // кол-во элементов на "работника" + 2 read-only строки
                                num_elem_per_worker * M + 2 * M,
                                MPI_CHAR,		// тип
                                i + 2,			// кому шлём
                                0,			// тэг
                                MPI_COMM_WORLD		// группа
                        )  != MPI_SUCCESS) {
                            perror("Ошибка передачи работнику\n");
                        }
                    }
                    // считает свою часть
                    // последние строки...
                    char* next_step_TABLE = new char [N * M];
                    for (uint32_t i = num_workers * num_elem_per_worker * M + M; i < M * N; i ++) {
                        next_step_TABLE[i] = next_step_cell(TABLE, N, M, i);
                    }
                    // ...и первую
                    for (uint32_t i = 0; i < M; i ++) {
                        next_step_TABLE[i] = next_step_cell(TABLE, N, M, i);
                    }

                    // принимает куски таблицы от работников, сливает в одну
                    for (int i = 0; i < num_workers; i ++) {
                        if (MPI_Recv(
                                next_step_TABLE + i * (num_elem_per_worker) * M + M, 	// куда пишем
                                num_elem_per_worker * M,			// кол-во элементов
                                MPI_CHAR, 		// тип
                                i + 2,	// от кого
                                0,			// тэг
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
                    steps_to_do --;
                }

                //////////////////////////////////////

                MPI_Request req;
                MPI_Irecv(
                        &master_order,              // куда пишем
                        sizeof(Order),              // размер
                        MPI_CHAR,                   // тип
                        0,                   // от кого
                        0,                // флаг
                        MPI_COMM_WORLD,             // группа
                        &req
                );
                int has_new_order = 0;
                // Если считать пока нечего - подождем приказа
                if (steps_to_do == 0) {
                    MPI_Wait(&req, MPI_STATUS_IGNORE);
                }
                MPI_Test(&req, &has_new_order, MPI_STATUS_IGNORE);

                if (has_new_order) {
                    switch(master_order.command){
                        /***
                         * ADD_STEPS
                         * Просто добавим шаги и продолжим работу
                         */
                        case ADD_STEPS: {
                            steps_to_do += master_order.new_steps;
                            break;
                        }
                            /***
                             * STATUS
                             * Отправляем таблицу
                             */
                        case STATUS: {
                            // можем выслать статус
                            MPI_Send(
                                    TABLE,
                                    N * M,
                                    MPI_CHAR,		// тип
                                    0,			// кому шлём
                                    0,			// тэг
                                    MPI_COMM_WORLD		// группа
                            );
                            break;
                        }
                            /***
                             * RESET и QUIT реализованы в STOP,
                             * здесь проходим дальше
                             */
                        case RESET: {}
                        case QUIT:{}
                            /***
                             * STOP
                             * Просто отправим информацию о шагах, остальное сделает мастер
                             */
                        case STOP: {
                            Order submaster_ans(RUN, steps_to_do, cur_step);
                            MPI_Send(
                                    &submaster_ans,
                                    sizeof(Order),
                                    MPI_CHAR,		// тип
                                    0,			// кому шлём
                                    0,			// тэг
                                    MPI_COMM_WORLD		// группа
                            );
                            // Обнуляем значения шагов
                            steps_to_do = 0;
                            cur_step = 0;
                            if (master_order.command == STOP) {
                                break;
                            }


                            // Для QUIT нужно остановить работников...
                            if (master_order.command == QUIT) {
                                // пересылаем строку со спецсимволом
                                TABLE[0] = '#';

                                for (int i = 0; i < num_workers; i ++) {
                                    if (MPI_Send(
                                            TABLE,
                                            num_elem_per_worker * M + M * 2,
                                            MPI_CHAR,
                                            i + 2,
                                            0,
                                            MPI_COMM_WORLD
                                    ) != MPI_SUCCESS) {
                                        perror("Ошибка заключительной передачи работнику.");
                                    }
                                }

                                MPI_Barrier(MPI_COMM_WORLD);
                                is_game_quit = true;

                                delete [] TABLE;
                                // ..и остановиться самому
                                MPI_Finalize();
                                return;
                            }

                            // Для RESET нужно очистить свою таблицу
                            N = 0;
                            M = 0;
                            delete [] TABLE;

                            break;
                        }
                        default: {
                            break;
                        }
                    }
                }
            }
        }
            ////////////////////////////////////////////////////////////////
            ///////// Работник
        default: {
            char* worker_table = new char [num_elem_per_worker * M + 2 * M];
            while(true) {
                // принимает сообщения от подмастерья
                if (MPI_Recv(
                        worker_table,		// куда пишем
                        num_elem_per_worker * M + 2 * M,	// кол-во элементов
                        MPI_CHAR,		// тип
                        1,			// от кого
                        0,			// тэг?
                        MPI_COMM_WORLD,	// группа
                        MPI_STATUS_IGNORE	// флаг
                ) != MPI_SUCCESS) {
                    perror("Ошибка получения от подмастерья\n");
                }

                // если пришла фейковая строка,
                // то это приказ остановиться
                if (worker_table[0] == '#') {
                    delete [] worker_table;

                    MPI_Barrier(MPI_COMM_WORLD);
                    is_game_quit = true;

                    MPI_Finalize();
                    return;
                }

                // считает свою часть
                char new_worker_table [num_elem_per_worker * M];
                for (uint32_t i = M; i < M + num_elem_per_worker * M; i ++) {
                    new_worker_table[i - M] = next_step_cell(
                            worker_table,
                            num_elem_per_worker + 2,      // строк в куске
                            M,      // длина строки
                            i);
                }

                // возвращает ответ подмастерью
                if (MPI_Send(
                        new_worker_table,     // откуда читаем
                        num_elem_per_worker * M, //сколько
                        MPI_CHAR,		// тип
                        1,			// кому шлём
                        0,			// тэг
                        MPI_COMM_WORLD		// группа
                )  != MPI_SUCCESS) {
                    perror("Ошибка передачи работнику\n");
                }
            }
        }
    }
}

/***
 * STATUS
 */
void CLifeGame::Status() {
    if (world_id || is_game_quit) {                 // обрабатывает только мастер
        return;
    }
    if (!is_game_started) {
        std::cout << "Игра еще не началась.\n";
        return;
    }
    // если игра не запущена, можно узнать статус
    // прямиком от мастера
    if (!is_game_running) {
        for (uint32_t i = 0; i < N; i++) {
            for (uint32_t j = 0; j < M; j++) {
                std::cout << TABLE[i * M + j] << " ";
            }
            std::cout << "\n";
        }
        std::cout << "\n";
        return;
    }
    // Запрашиваем статус у подмастерья

    Order get_status_order(STATUS);
    MPI_Request status_req;
    MPI_Isend(
            &get_status_order,
            sizeof(Order),
            MPI_CHAR,
            1,
            0,
            MPI_COMM_WORLD,
            &status_req
    );

    int is_status_ready = 0;
    MPI_Test(&status_req, &is_status_ready, MPI_STATUS_IGNORE);

    if (!is_status_ready) {
        std::cout << "В данный момент производится просчет итераций\n";
    }
    else {
        // можем принять - принимаем
        MPI_Recv(
                TABLE,
                N * M,
                MPI_CHAR,
                1,
                0,
                MPI_COMM_WORLD,
                MPI_STATUS_IGNORE
        );

        for (uint32_t i = 0; i < N; i++) {
            for (uint32_t j = 0; j < M; j++) {
                std::cout << TABLE[i * M + j] << " ";
            }
            std::cout << "\n";
        }
        std::cout << "\n";
    }
}

/***
 * STOP
 * запускается извне или изнутри
 * без параметра = обычный STOP,
 * с параметром отправит подмастерью соответствующую cmd
 */
void CLifeGame::Stop(COMMANDS cmd=STOP) {
    if (world_id || is_game_quit) {                 // обрабатывает только мастер
        return;
    }
    if (!is_game_started) {
        std::cout << "Игра еще не началась.\n";
        return;
    }

    // Этой команды нужно дождаться!
    Order stop_order(cmd, 0, 0);
    MPI_Send(
            &stop_order,
            sizeof(Order),
            MPI_CHAR,
            1,
            0,
            MPI_COMM_WORLD
    );

    // Принимаем ответ о шагах
    Order sub_answer;
    MPI_Recv(
            &sub_answer,
            sizeof(Order),
            MPI_CHAR,
            1,
            0,
            MPI_COMM_WORLD,
            MPI_STATUS_IGNORE
    );

    std::cout << "С начала игры прошло шагов: " << sub_answer.finished_steps << ".\n";
    if (sub_answer.new_steps) {
        std::cout << "Итерация была прервана\n";
    }
    else {
        std::cout << "Все требуемые шаги итерации были выполнены\n";
    }
}

/***
 * RESET
 * аналогично STOP отправит
 * подмастерью cmd,
 * очистит параметры игры
 */
void CLifeGame::Reset(COMMANDS cmd=RESET) {
    if (is_game_quit) {
        return;
    }
    this->Stop(cmd);
    // чистим таблицу
    // и значения
    delete [] TABLE;
    N = 0;
    M = 0;
    is_game_started = false;
    is_game_running = false;
}

/***
 * QUIT
 */
void CLifeGame::Quit() {
    if (is_game_quit) {
        return;
    }
    // чистим таблицу
    this->Reset(QUIT);
    is_game_quit = true;

    MPI_Barrier(MPI_COMM_WORLD);

    MPI_Finalize();
}