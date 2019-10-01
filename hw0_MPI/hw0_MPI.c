/*
Задача №0 (Сумма элементов массива).
1. Вывести на экран в столбик суммы, посчитанные процессами.
2. Вывести на экран сумму всех элементов, посчитанную последовательно.
   Сравнить её с суммой, полученной параллельно.
*/

#include <mpi.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

///////////////////////////////
// Запись частичных сумм

void write_results(int id, uint64_t result) {
	printf("Частичная сумма процесса №%d равна %ld\n", id, result);
}

//////////////////////////////
// MAIN

int main (int argc, char** argv) {
	
	const uint32_t N = atoi(argv[1]);		// размер массива
	
	// Инициализация MPI
	if (MPI_Init (NULL, NULL) != MPI_SUCCESS) {
		perror("Ошибка инициализации MPI ");
		return 1;
	}
 	
	int world_size, world_id;
	MPI_Comm_size(MPI_COMM_WORLD, &world_size);	// кол-во процессов
	MPI_Comm_rank(MPI_COMM_WORLD, &world_id);	// локальный id
	
	// Информация о разделении массива
	const uint32_t num_workers = world_size - 1;	// кол-во "работников"
	const uint32_t num_for_main = N % num_workers;	// избыток посчитает "основной"
	const uint32_t num_elem_per_worker = (N - num_for_main) / num_workers;
	uint64_t loc_sum = 0;				// частичная сумма, локальная

	// "Основной" процесс
	if (world_id == 0) {
		uint32_t arr[N];
		uint64_t check_summ = 0;
		
		// заполнит массив, посчитает сумму для проверки
		for (int i = 0; i < N; i ++) {
			arr[i] = i;
			check_summ += arr[i];
		}
		

		// рассылает подмассивы
		for (int i = 0; i < num_workers; i ++) {
			if (MPI_Send(
			      arr + i * (num_elem_per_worker), // arr + offset
			      num_elem_per_worker,	// кол-во элементов на "работника"
			      MPI_UNSIGNED,		// тип
			      i + 1,			// кому шлём
			      0,			// тэг
			      MPI_COMM_WORLD		// группа
			    )  != MPI_SUCCESS) {
				perror("Ошибка передачи работнику\n");
			} 
		}
		
		// считает свою часть
		for (int i = num_workers * num_elem_per_worker; i < N; i ++) {
			loc_sum += arr[i];
		}
		write_results(world_id, loc_sum);

		// принимает частичные суммы "работников"
		uint64_t worker_loc_sum = 0;
		for (int i = 0; i < num_workers; i ++) {
			if (MPI_Recv(
			  &worker_loc_sum, 	// куда пишем
			  1,			// кол-во элементов
			  MPI_LONG, 		// тип
			  i + 1,		// от кого
			  0,			// тэг?
			  MPI_COMM_WORLD,	// группа
			  MPI_STATUS_IGNORE	// флаг
			    ) != MPI_SUCCESS) {
				perror("Ошибка получения от работника\n");
			}
			write_results(i + 1, worker_loc_sum);
			// к своей сумме прибавляет полученную
			loc_sum += worker_loc_sum;
		}

		// сверяется с корректной суммой
		printf("Верная сумма равна %d, мы получили %d\n", check_summ, loc_sum );
		loc_sum == check_summ ? printf("Success!!!!\n") : printf("epic fail\n");
	}
	// Процессы для подчета частичных сумм, "работники"
	else {
		// принимают сообщение от "основного"
		uint32_t worker_arr[num_elem_per_worker];
		if (MPI_Recv(
		  worker_arr,		// куда пишем
		  num_elem_per_worker,	// кол-во элементов
		  MPI_UNSIGNED,		// тип
		  0,			// от кого
		  0,			// тэг?
		  MPI_COMM_WORLD,	// группа
		  MPI_STATUS_IGNORE	// флаг	
		    ) != MPI_SUCCESS) {
			perror("Ошибка получения от основного\n");
			return 1;
		}
		
		// считают свою частичную сумму
		for (int i = 0; i < num_elem_per_worker; i ++) {
			loc_sum += worker_arr[i];
		}
		
		// отправляют её "основному"
		if (MPI_Send(
		  &loc_sum,		// что отправляем?
		  1, 			// кол-во элементов
		  MPI_LONG,		// тип
		  0,			// кому
		  0,			// тэг
		  MPI_COMM_WORLD	// группа
		    ) != MPI_SUCCESS) {
			perror("Ошибка передачи основному\n");
		}	
	}
	MPI_Finalize();
	return 0;
}

