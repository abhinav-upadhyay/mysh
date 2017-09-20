/*-
 * Copyright (c) 2017 Abhinav Upadhyay <er.abhinav.upadhyay@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <curses.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <util.h>
#include <unistd.h>

#include <sys/wait.h>

#include "libspell.h"
#include "trie.h"

const char *PROMPT = "$>";

typedef enum command_type {
	CMD = 0,
	MAN,
	PKG
} command_type;

spell_t *spell_cmd_map[3];
const char *spell_dicts[] = {
	"./commands.txt",
	"./mans.txt",
	"./pkgs.txt"
};



static int
exec_proc(char *procname, char **args)
{
	int status;
	pid_t pid = fork();

	if (pid < 0)
		err(EXIT_FAILURE, "fork failed");
	if (pid == 0) {
		execvp(procname, args);
		exit(errno);
	}

	wait(&status);
	if WIFEXITED(status)
		return WEXITSTATUS(status);
	return status;

}

static void
free_args(char **args)
{
	if (args == NULL)
		return;

	size_t i = 0;
	while (args[i] != NULL)
		free(args[i++]);
	free(args);
}

static word_list *
get_wordlist(const char *file)
{
	FILE *f;
	char *line = NULL;
	size_t count;
	size_t linesize = 0;
	size_t wordsize = 0;
	ssize_t bytes_read;
	word_list *dictionary_list = NULL;
	word_list *tail;
	word_list *node;

	f = fopen(file, "r");
	if (f == NULL) {
		warn("Failed to open commands.txt");
		return NULL;
	}

	while ((bytes_read = getline(&line, &linesize, f)) != -1) {
		line[bytes_read - 1] = 0;
		node = emalloc(sizeof(*node));
		node->word = estrdup(line);
		node->next = NULL;
		if (dictionary_list == NULL) {
			dictionary_list = node;
			tail = node;
		} else {
			tail->next = node;
			tail = node;
		}
	}
	free(line);
	fclose(f);
	return dictionary_list;
}

static spell_t *
_spell_init(command_type cmd_type)
{
	spell_t *spellt;
	const char *filename = spell_dicts[cmd_type];
	word_list *dictionary_list = get_wordlist(spell_dicts[cmd_type]);
	spellt = spell_init2(dictionary_list, NULL);
	free_word_list(dictionary_list);
	return spellt;
}

static void
_spell_destroy(void)
{
	spell_destroy(spell_cmd_map[CMD]);
	spell_destroy(spell_cmd_map[MAN]);
	spell_destroy(spell_cmd_map[PKG]);
}

static void
print(size_t count, ...)
{
	va_list args;
	va_start(args, count);
	size_t i;
	echo();
	for (i = 0; i < count; i++)
		printw( "%s", va_arg(args, char *));
	refresh();
	noecho();
}

static void
print_arr(char **arr)
{
	size_t i = 0;
	if (arr == NULL)
		return;

	echo();
	while (arr[i] != NULL) {
		printw("%s ", arr[i++]);
	}
	refresh();
	noecho();
}

static size_t
get_maxwidth(char **l)
{
	size_t max = 0;
	size_t i = 0;
	while (l[i] != 0) {
		size_t len = strlen(l[i++]);
		if (len > max)
			max = len;
	}
	return max;
}

/**
 * If there is more than one possible auto completions and tabkey count is 1
 * just return the list. If the user hits tab again, the caller will pass the
 * same list and we will print it out. In rest of the cases, we do the auto-
 * completion and reutnr NULL
 */
static char ** 
do_autocompletion(char **args, size_t args_offset, char **cmd, size_t *cmd_offset,
		size_t *cmd_size, char **suggestions, size_t tabkey_count)
{
	if (cmd_offset == 0)
		return NULL;

	command_type cmdtype;
	if (args_offset == 0)
		cmdtype = CMD;
	else if (strcmp(args[0], "man") == 0)
		cmdtype = MAN;
	else if(strncmp(args[0], "pkg_", 4) == 0)
		cmdtype = PKG;

	if (suggestions == NULL)
		suggestions = get_completions(spell_cmd_map[cmdtype], *cmd);
	if (suggestions == NULL)
		return NULL;

	if (suggestions[1] == NULL) {
		size_t len = strlen(*cmd);
		print(1, *(suggestions) + len);
		if (cmd_offset + len > cmd_size) {
			*cmd = realloc(*cmd, (*cmd_offset) + len + 1);
			*cmd_size = (*cmd_offset) + len;
		}
		memcpy((*cmd) + (*cmd_offset), *(suggestions) + len, len + 1);
		*cmd_offset += len + 1;
		free_list(suggestions);
		return NULL;
	}

	if (tabkey_count == 1)
		return suggestions;

	size_t maxwidth = get_maxwidth(suggestions); 
	size_t i = 0;
	size_t colnum = 0;
	if (suggestions[i + 1] != NULL)
		print(1, "\n");

	while(suggestions[i] != NULL) {
		if (colnum > 0)
			print(1, "\t");
		if (colnum == 3) {
			print(1, "\n");
			colnum = 0;
		}
		colnum++;
		print(1, suggestions[i++]);
		if (suggestions[i]) {
			echo();
			printw("%-*s", maxwidth - strlen(suggestions[i - 1]), "");
			refresh();
			noecho();
		}
	}

	free_list(suggestions);
	print(2, "\n", PROMPT);
	print_arr(args);
	print(1, *cmd);
	return NULL;
}

int
main(int argc, char** argv)
{
	char ch;
	char *cmd = NULL;
	char **args = NULL;
	char **autocompletions = NULL;
	size_t cmd_size; 
	size_t args_size;
	size_t cmd_offset;
	size_t args_offset;
	size_t tabkey_count = 0;
	spell_t *cmd_spellt = _spell_init(CMD);
	spell_t *man_spellt = _spell_init(MAN);
	spell_t *pkg_spellt = _spell_init(PKG);
	spell_cmd_map[CMD] = cmd_spellt;
	spell_cmd_map[MAN] = man_spellt;
	spell_cmd_map[PKG] = pkg_spellt;
	WINDOW *win = initscr();
	keypad(win, TRUE);
	cbreak();

	while (1) {
		print(1, PROMPT);
		cmd_offset = 0;
		args_offset = 0;
		cmd_size = 32;
		args_size = 8;
		args = NULL;
		cmd = ecalloc(1, cmd_size);

		while((ch = wgetch(win)) != EOF) {
			if (cmd == NULL) {
				cmd = ecalloc(1, cmd_size);
				cmd_offset = 0;
			}

			if (ch == KEY_BACKSPACE || ch == KEY_DC || ch == 127 || ch == 8) {
				if (cmd_offset > 0) {
					echo();
					int y = getcury(win);
					deleteln();
					move(y, 0);
					refresh();
					noecho();
					cmd[--cmd_offset] = 0;
					print(2, PROMPT, cmd);
				}
				continue;
			}

			if (ch == '\n') {
				print(1, "\n");
				cmd[cmd_offset] = 0;
				if (args == NULL) {
					args = ecalloc(args_size, sizeof(*args));
					args_offset = 0;
				} else if (args_offset == args_size) {
					args_size += 4;
					args = erealloc(args, sizeof(*args) * args_size);
					memset(args, 0, sizeof(*args) * args_size);
				}
				args[args_offset++] = cmd;
				args[args_offset] = NULL;
				if (strlen(cmd) == 0) {
					free_args(args);
					args = NULL;
					cmd = NULL;
					break;
				}
				int exit_status = exec_proc(args[0], args);
				if (exit_status == ENOENT) {
					word_list *spell_suggestions = spell_get_suggestions_slow(spell_cmd_map[CMD], args[0], 1);
					if (spell_suggestions != NULL) {
						echo();
						printw("Did you mean %s?\n", spell_suggestions->word);
						refresh();
						noecho();
						free_word_list(spell_suggestions);
					}
				}
				free_args(args);
				args = NULL;
				cmd = NULL;
				break;
			} 
			if (ch == ' ') {
				cmd[cmd_offset] = 0;
				if (args == NULL)
					args = ecalloc(args_size, sizeof(*args));
				else if (args_offset == args_size) {
					args_size += 4;
					args = realloc(args, sizeof(*args) * args_size);
					memset(args, 0, sizeof(*args) * args_size);
				}
				args[args_offset++] = cmd;
				cmd = NULL;
				cmd_offset = 0;
				print(1, " ");
				continue;
			}
			if (ch == '\t') {
				tabkey_count++;
				autocompletions = do_autocompletion(args, args_offset, &cmd, 
						&cmd_offset, &cmd_size, autocompletions, tabkey_count);
				if (tabkey_count == 2)
					tabkey_count = 0;
				continue;
			}
			
			if (cmd_offset == cmd_size) {
				cmd_size *= 2;
				cmd = realloc(cmd, cmd_size);
			}

			cmd[cmd_offset++] = ch;
			echo();
			printw("%c", ch);
			refresh();
			noecho();
		}
		if (feof(stdin))
			exit(0);
		if (ch == EOF) {
			endwin();
			exit(0);
		}
	}
	_spell_destroy();
	return 0;
}
