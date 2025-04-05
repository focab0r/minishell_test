#include "minishell.h"

/*
Comprueba si se trata de un builtin
Si es exit mata los procesos en segundo plano y libera la memoria
En caso de ser necesario redirige el mensaje de error de mandato no encontrado
*/
void	exec_builtin(tcommand t, char *error_file)
{
	int		error_fd;
	int 	err_fd;
	int	i;
	int j;
	
	err_fd = dup(STDERR_FILENO);
	if (t.argc > 0)
	{
		if (strncmp(t.argv[0], "cd", 3) == 0)
			builtin_cd(t);
		else if (strncmp(t.argv[0], "jobs", 5) == 0)
			builtin_jobs(pid_stock);
		else if (strncmp(t.argv[0], "fg", 3) == 0)
			builtin_fg(t);
		else if (strncmp(t.argv[0], "exit", 5) == 0)
		{
			i = 0;
			while (i < pid_stock->background_commands)
			{
				j = 0;
				while (j < pid_stock->ncommands[i])
				{
					kill(pid_stock->waitpid_estructure[i][j], SIGKILL);
					j++;
				}
				i++;
			}
			refresh_pids_cache(-1);
			exit(0);
		}
		else
		{
			if (error_file)
			{
				error_fd = open(error_file, O_CREAT | O_RDWR | O_TRUNC, 0644);
				if (error_fd < 0)
				{
					fprintf(stderr, "%s: Error. ", error_file);
					fflush(stderr);
					perror("");
					return;
				}
				dup2(error_fd, STDERR_FILENO);
				close(error_fd);
			}
			fprintf (stderr, "%s: No se encuentra el mandato\n", t.argv[0]);
		}
	}
	dup2(err_fd, STDERR_FILENO);
	close(err_fd);
}

int	prepare_last_command_files(int *last_fd, int *error_fd, char *output_file, char *error_file)
{
	if (output_file)
	{
		*last_fd = open(output_file, O_CREAT | O_RDWR | O_TRUNC, 0644);
		if (*last_fd < 0)
		{
			fprintf(stderr, "%s: Error. ", output_file);
			fflush(stderr);
			perror("");
			return (1);
		}
	}
	else
		*last_fd = 1;
	if (error_file)
	{
		*error_fd = open(error_file, O_CREAT | O_RDWR | O_TRUNC, 0644);
		if (*error_fd < 0)
		{
			fprintf(stderr, "%s: Error. ", error_file);
			fflush(stderr);
			perror("");
			return (1);
		}
	}
	return (0);
}

/*
Crea el pipe y, posteriormente, el hijo.
En el hijo redirige las salidas al pipe o a los ficheros de salida correspondiente
En el padre redirige el stdin al pipe para el proximo mandato o cierra los fds de salida y de error si aplica
*/
int	pipex(char **argv, int argc, int last_command, char *output_file, char *error_file, int background)
{
	int		fd[2];
	pid_t	pid;
	char	**command;
	int		i;
	int		last_fd;
	int		error_fd;

	last_fd = 0;
	if (last_command)
	{
		if (prepare_last_command_files(&last_fd, &error_fd, output_file, error_file))
			return (0);
	}
	else
	{
		if (pipe(fd) == -1)
		{
			perror("pipe");
			exit(EXIT_FAILURE);
		}
	}
	pid = fork();
	if (pid == -1)
	{
		perror("fork");
		exit(EXIT_FAILURE);
	}
	if (pid == 0)
	{
		if (background)
		{
			signal(SIGINT, SIG_IGN);
			signal(SIGQUIT, SIG_IGN);
		}
		else
		{
			signal(SIGINT, SIG_DFL);
			signal(SIGQUIT, SIG_DFL);
		}
		if (!last_command)
		{
			close(fd[0]);
			dup2(fd[1], STDOUT_FILENO);
			close(fd[1]);
		}
		if (output_file)
		{
			dup2(last_fd, STDOUT_FILENO);
			close(last_fd);
		}
		if (error_file)
		{
			dup2(error_fd, STDERR_FILENO);
			close(error_fd);
		}
		command = (char **) malloc ((argc + 1) * sizeof(char *));
		if (command == NULL)
		{
			perror("Memory allocation error");
			exit(1);
		}
		i = 0;
		while (i < argc)
		{
			command[i] = argv[i];
			i++;
		}
		command[i] = NULL;
		execvp(argv[0], command);
		exit(42);
	}
	else
	{
		if (!last_command)
		{
			close(fd[1]);
			dup2(fd[0], STDIN_FILENO);
			close(fd[0]);
		}
		if (last_command && last_fd != 1)
			close(last_fd);
		if (error_file)
			close(error_fd);
		return (pid);
	}
}

/*
Recorre todos los mandatos de la linea y se los va pasando a pipex o a exec_builtin
Si la línea no está en bg se espera por los procesos 
*/
int	*execute_commands(tline *line)
{
	int		infd;
	int		fd;
	int		i;
	int		*waitpid_list;
	int		status;

	infd = dup(STDIN_FILENO);
	if (line->redirect_input != NULL)
	{
		fd = open(line->redirect_input, O_RDONLY);
		if (fd < 0)
		{
			fprintf(stderr, "%s: Error. ", line->redirect_input);
			fflush(stderr);
			perror("");
			return (NULL);
		}
		dup2(fd, STDIN_FILENO);
		close(fd);
	}
	waitpid_list = (int *) calloc (line->ncommands, sizeof(int));
	if (waitpid_list == NULL)
	{
		perror("Memory allocation error");
		exit(1);
	}
	i = 0;
	while (i < line->ncommands)
	{
		if (line->commands[i].filename == NULL && line->ncommands - 1 == i)
			exec_builtin(line->commands[i], line->redirect_error);
		else if (line->commands[i].filename == NULL)
			exec_builtin(line->commands[i], NULL);
		else if (line->ncommands - 1 == i)
			waitpid_list[i] = pipex(line->commands[i].argv, line->commands[i].argc, 1, line->redirect_output, line->redirect_error, line->background);
		else
			waitpid_list[i] = pipex(line->commands[i].argv, line->commands[i].argc, 0, NULL, NULL, line->background);
		i++;
	}
	if (!line->background)
	{
		i = 0;
		while (i < line->ncommands)
		{
			if (waitpid_list[i])
			{
				waitpid(waitpid_list[i], &status, 0);
			}
			i++;
		}
	}
	dup2(infd, STDIN_FILENO);
	close(infd);
	if (line->background)
		return (waitpid_list);
	else
		return (NULL);
}
