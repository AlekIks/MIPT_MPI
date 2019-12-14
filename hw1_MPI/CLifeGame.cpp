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

    if (is_game_started && !world_id) {
        std::cout << "Игра уже была инициализирована.\n";
    }

    // это общие для всех параметры
    N = n, M = m;
    TABLE = new char[N * M + 2];
    is_game_started = true;

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
            int steps_to_do = steps_num;  // оставшиеся шаги
            Order master_order; // приказы мастера

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
                                // // кол-во элементов на "работника" + 2 read-only строки
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
                        MPI_ANY_TAG,                // флаг
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
                            MPI_Request sub_req;
                            // можем выслать статус
                            MPI_Isend(
                                    &TABLE,
                                    N * M,
                                    MPI_CHAR,		// тип
                                    0,			// кому шлём
                                    0,			// тэг
                                    MPI_COMM_WORLD,		// группа
                                    &sub_req
                            );
                        }
                        /***
                         * STOP, RESET, QUIT
                         * Просто отправим информацию о шагах, остальное сделает мастер
                         */
                        case STOP || RESET || QUIT: {
                            Order submaster_order(RUN, steps_to_do, cur_step);
                            MPI_Send(
                                    &TABLE,
                                    N * M,
                                    MPI_CHAR,		// тип
                                    0,			// кому шлём
                                    0,			// тэг
                                    MPI_COMM_WORLD		// группа
                            );
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
            delete [] worker_table;
            break;
        }
    }
}

/***
 * STATUS
 */
void CLifeGame::Status() {
    if (world_id) {                 // обрабатывает только мастер
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
        return;
    }
    // Запрашиваем статус у подмастерья

    Order get_status_order(STATUS);
    MPI_Request req;
    MPI_Isend(
            &get_status_order,
            sizeof(Order),
            MPI_CHAR,
            1,
            0,
            MPI_COMM_WORLD,
            &req
    );

    // Получаем, если можем
    MPI_Request status_req;
    MPI_Irecv(
            &TABLE,
            N * M,
            MPI_CHAR,
            1,
            0,
            MPI_COMM_WORLD,
            &status_req
    );


    int is_status_ready = 0;
    MPI_Wait(&status_req, MPI_STATUS_IGNORE);
    MPI_Test(&status_req, &is_status_ready, MPI_STATUS_IGNORE);

    if (!is_status_ready) {
        std::cout << "В данный момент производится просчет итераций\n";
    }
    else {
        for (uint32_t i = 0; i < N; i++) {
            for (uint32_t j = 0; j < M; j++) {
                std::cout << TABLE[i * M + j] << " ";
            }
            std::cout << "\n";
        }
    }
}