#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <sys/wait.h>

// Ile maksymalnie zadan - potrzebne przy definicji tabeli z zadaniami
#define MAX_TASKS 100
// Ile maksymalnie znakow ma jedna linia polecenia - do przechowywania stringa z poleceniem w strukturze Task
#define MAX_LINE 256

// Struktura do przechowywania pojedynczego zadania, przechowuje komende, tryb (0, 1, 2) oraz czas odpalenia.
typedef struct {
    char command[MAX_LINE];
    int mode;
    time_t run_at;
} Task;

// Tabela przechowujaca zadania do wykonania
Task tasks[MAX_TASKS];
// licznik zadan
int task_count = 0;
// sciezka do pliku z outputem
char *outfile_path;
// sciezka do pliku z zadaniami
char *taskfile_path;
// flaga uzywana do przerwania dzialaniu daemona, potem uzywana w funkcji main w loopie while(running)
int running = 1;


// funkcja przetasowania zadan w tabeli, aby zgodnie z trescia wykonac je w losowej kolejnosci
void shuffle_tasks(Task *tasks) {
    for (int i = task_count - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        // zamieniamy
        Task temp = tasks[i];
        tasks[i] = tasks[j];
        tasks[j] = temp;
    }
}

// ladowanie zadan z pliku taskfile
void load_tasks() {
    FILE *f = fopen(taskfile_path, "r");
    if (!f) {
        syslog(LOG_ERR, "Failed to open task file");
        exit(EXIT_FAILURE);
    }

    char line[MAX_LINE];
    int hour_start, min_start, hour_end, min_end;
    fgets(line, sizeof(line), f);
    sscanf(line, "%d:%d;%d:%d", &hour_start, &min_start, &hour_end, &min_end);

    // zapisujemy obecny czas w zmiennej
    time_t now = time(NULL);
    // struct tm - zdefiniowany w time.h, start_tm zmienna ktora przechowuje czas
    struct tm start_tm = *localtime(&now);
    // uzupelniamy structa danymi godziny i minuty zdefiniowanej w taskfile jako pierwsza
    start_tm.tm_hour = hour_start;
    start_tm.tm_min = min_start;
    start_tm.tm_sec = 0;
    // konwertujemy na timestamp, aby latwo porownywac czas
    time_t start_time = mktime(&start_tm);


    // to samo co wyzej tylko do czasu zakonczenia zdefiniowanego w taskfile
    struct tm end_tm = *localtime(&now);
    end_tm.tm_hour = hour_end;
    end_tm.tm_min = min_end;
    end_tm.tm_sec = 0;
    time_t end_time = mktime(&end_tm);

    task_count = 0;
    // wczytujemy kolejne linie z pliku, ktore sa komendami. Jednoczesnie sprawdzamy czy nie przekraczamy maksymalnej liczby zadan (100)
    while (fgets(line, sizeof(line), f) && task_count < MAX_TASKS) {
        // rozdzielamy linie, separatorem jest dwukropek lub nowa linia. Pierwszy to komenda, drugi to tryb
        char *cmd = strtok(line, ":\n");
        char *mode_str = strtok(NULL, ":\n");
        // sprawdzamy poprawnosc zdefiniowanej linii
        if (cmd && mode_str) {
            // kopiujemy polecenie do struktury task o indeksie task_count.
            strncpy(tasks[task_count].command, cmd, MAX_LINE);
            // za pomoca atoi, konwertujemy stringa do inta i nadajemy tryb
            tasks[task_count].mode = atoi(mode_str);
            tasks[task_count].run_at = 0; // tymczasowo ustawiamy na 0, w dalszej czesci ustawiamy czas startu aby dodac losowosc do zadan
            task_count++;
        }
    }
    fclose(f);

    // czas miedzy zadaniami to 10 sekund, wiec task_count * 10 tworzy nam ten odstep
    // odejmujemy zdefiniowany koniec czasu zadania od jego poczatku oraz potrzebny czas miedzy zadaniami

    // moze dojsc do sytuacji gdzie mamy zbyt wiele zadan na zbyt male okienko czasowe (bo uwzgledniamy przerwy) 
    // wowczas przerywamy dzialanie daemona
    int range = difftime(end_time, start_time) - task_count * 10;
    if (range < 0) {
        syslog(LOG_ERR, "Not enough time to schedule all of the defined tasks with a 10s window between each one. ");
        exit(1);
    }
    
    // losujemy kolejnosc zadan
    shuffle_tasks(tasks);
    // nadajemy losowy czas wykonania wczytanym zadaniom
    srand(time(NULL));

    // pierwszy task wykona sie o podanej godzinie, kolejne w odstepie po 10 sekund
    tasks[0].run_at = start_time;
    for (int i = 1; i < task_count; i++) {
        tasks[i].run_at = tasks[i-1].run_at + 10;
    }
}


// obsluga sigint przy wyslaniu sygnalu do daemona (kill -SIGINT [...])
void handle_sigint(int sig) {
    // zapisanie do logow systemowych
    syslog(LOG_INFO, "Received SIGINT. Exiting after current task.");
    running = 0;
}

// TODO przeladowanie pliku z zadaniami
void handle_sigusr1(int sig) {
    syslog(LOG_INFO, "Received SIGUSR1. Reloading tasks.");
    task_count = 0;
    load_tasks();
}

// wypisanie pozostalych zadan do logow systemowych
void handle_sigusr2(int sig) {
    syslog(LOG_INFO, "Received SIGUSR2. Remaining tasks:");
    for (int i = 0; i < task_count; i++) {
        if (tasks[i].run_at != 0) {
            syslog(LOG_INFO, "Task: %s", tasks[i].command);
        }
    }
}



// wywolywanie pojedynczego zadania poprzez utworzenie potoku
void execute_task(Task *task) {
    pid_t pid = fork();
    // jesli pid to zero, znajdujemy sie w procesie potomnym, else proces rodzica
    if (pid == 0) {
        // otwieramy plik outfile, write-only, append, tworzymy plik jesli nie istnieje pod dana sciezka, 0644 dostep rwrr
        int out_fd = open(outfile_path, O_WRONLY | O_APPEND | O_CREAT, 0644);
        // w zaleznosci od trybu, przekierowujemy odpowiednie outputy do pliku
        if (task->mode == 0) {
            // przekierowujemy stdout do pliku
            dup2(out_fd, STDOUT_FILENO);
        } else if (task->mode == 1) {
            // przekierowujemy sterr do pliku
            dup2(out_fd, STDERR_FILENO);
        } else {
            // przekierowujemy stdout i stderr do pliku
            dup2(out_fd, STDOUT_FILENO);
            dup2(out_fd, STDERR_FILENO);
        }
        // zapisujemy informacje o uruchomionej komendzie do outfile
        dprintf(out_fd, "\n[Command: %s]\n", task->command);
        // uruchamiamy bash'a z opcja -c gdzie przekazujemy argument w postaci komendy aktualnego task'a
        execl("/bin/sh", "sh", "-c", task->command, NULL);
        // w przypadku gdy np nie znajdziemy komendy, proces potomny opusci proces rodzica
        exit(1);
    } else {
        int status;
        // oczekiwanie na zakonczenie wywolanego potomnego task'a
        waitpid(pid, &status, 0);
        syslog(LOG_INFO, "Executed: %s, Exit code: %d", task->command, WEXITSTATUS(status));
    }
}

int main(int argc, char *argv[]) {

    // w przypadku braku podania sciezki do taskfile oraz outfile, daemon nie wywola sie
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <taskfile> <outfile>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // przypisujemy sciezki do plikow z komendy do zmiennych
    taskfile_path = argv[1];
    outfile_path = argv[2];

    // tylko proces potomny kontynuuje dzialanie programu - demonizacja
    pid_t pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);

    // demon moze tworzyc pliki z pelnymi uprawnieniami
    umask(0);
    // otwieramy polaczenie z syslogiem, ustawiamy nazwe, pid oraz typ jako daemon
    openlog("minicron", LOG_PID, LOG_DAEMON);

    // odlaczamy proces od dzialania terminala, zmieniamy working directory na '/'
    pid_t sid = setsid();
    if (sid < 0) exit(EXIT_FAILURE);
    if ((chdir("/")) < 0) exit(EXIT_FAILURE);

    // przekierowujemy standardowe deskrypory na /dev/null, blokujac komunikacje daemona z terminalem
    int devnull = open("/dev/null", O_RDWR);
    if (devnull != -1) {
        dup2(devnull,STDIN_FILENO);
        dup2(devnull,STDOUT_FILENO);
        dup2(devnull,STDERR_FILENO);
        if (devnull > 2) close(devnull);
    }
    

    // definicje sygnalow z tresci zadania, podpinamy je pod wczesniej zdefiniowane voidy
    signal(SIGINT, handle_sigint);
    signal(SIGUSR1, handle_sigusr1);
    signal(SIGUSR2, handle_sigusr2);

    // wczytujemy taski z pliku
    load_tasks();



    while (running) {
        // pobieramy aktualny czas
        time_t now = time(NULL);
        // daemon bedzie dzialal (lub spal) przez 15min, dajac uzytkownikowi szanse na np. przeladowanie taskfile
        time_t next = now + 900;
        for (int i = 0; i < task_count; i++) {
            // nadszedl czas wykonania zadania i nie jest oznaczone jako wykonane (run_at = 0)
            if (tasks[i].run_at <= now && tasks[i].run_at != 0) {
                execute_task(&tasks[i]);
                tasks[i].run_at = 0;
            }
            // wykonujemy zadanie priorytetowo, wiec aktualizujemy next aby odpowiednio wybudzic daemona do jego wykonania
            else if (tasks[i].run_at > now && tasks[i].run_at < next) {
                next = tasks[i].run_at;
            }
        }
        // czekamy na nastepne uruchomienie
        sleep(next - now);
    }

    // zamykamy syslog po zakonczeniu dzialania daemona
    closelog();
    return 0;
}

