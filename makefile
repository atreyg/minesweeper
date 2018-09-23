TARGET = client server
CC = gcc
CFLAGS = -Wall -Wextra -g
normal: $(TARGET)
client: client.c
	$(CC) $(CFLAGS) client.c minesweeper_logic.c -o client
server: server.c
	$(CC) $(CFLAGS) server.c minesweeper_logic.c -o server
clean:
	$(RM) $(TARGET)
