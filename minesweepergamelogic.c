#include <stdio.h>
#include "stdlib.h"
#include <stdbool.h>

#define NUM_TILES_X 9
#define NUM_TILES_Y 9
#define NUM_MINES 10

typedef struct tile_struct {
    int adjacent_mines;
    bool revealed;
    bool is_mine;
    bool flagged;
} Tile;

typedef struct game_struct{
    int mines_left;
    bool gameOver;
    Tile tiles[NUM_TILES_X][NUM_TILES_Y];
} GameState;


void initialise_game(GameState* game);
void place_mines(GameState* game);
void increase_number_of_adjacent_mines(GameState* game, int row, int column);
void reveal_tile(GameState* game, int row, int column);
void place_flag(GameState* game, int row, int column);
void check_winning_condition(GameState* game);
void search_tiles(GameState* game, int row, int column);
void game_over(GameState* game);
void print_game_state(GameState* game);



void initialise_game(GameState* game) {
    game->mines_left = NUM_MINES;
    game->gameOver = false;
    for (int row = 0; row < NUM_TILES_Y; row++) {
        for (int column = 0; column < NUM_TILES_X; column++) {
            Tile* tile = &game->tiles[row][column];
            tile->adjacent_mines = 0;
            tile->revealed = false;
            tile->is_mine = false;
            tile->flagged = false;
        }
    }

    place_mines(game);
    //time = 0
    //reset and allocate memory
}

void place_mines(GameState* game) {
    for (int i = 0; i < NUM_MINES; i++) {
        int row, column;
        do {
            row = rand() % NUM_TILES_X;
            column = rand() % NUM_TILES_Y;
        } while (game->tiles[row][column].is_mine);
        game->tiles[row][column].is_mine = true;
        increase_number_of_adjacent_mines(game, row, column);
    }
}

void increase_number_of_adjacent_mines(GameState* game, int row, int column) {
    for (int i = -1; i <= 1; i++) {
		for (int j = -1; j <= 1; j++) {
            if (row + i >= 0 && column + j >= 0 && row + i < NUM_TILES_Y && column + j < NUM_TILES_X) {
			    game->tiles[row + i][column + j].adjacent_mines++;
            }
		}
	}
}

void reveal_tile(GameState* game, int row, int column) {
    if (row >= 0 && column >= 0 && row < NUM_TILES_Y && column < NUM_TILES_X) {

        Tile* tile = &game->tiles[row][column];
        if (tile->revealed) {
            return;
        }
        tile->revealed = true;

        if (tile->adjacent_mines == 0) {

            for (int i = -1; i <= 1; i++) {
                for (int j = -1; j <= 1; j++) {
                    reveal_tile(game, row + i, column + j);
                }
            }
        }
    }
}

void place_flag(GameState* game, int row, int column) {
    if (row >= 0 && column >= 0 && row < NUM_TILES_Y && column < NUM_TILES_X) {
        Tile* tile = &game->tiles[row][column];
      
        if (tile->is_mine) {
            tile->flagged = true;
            game->mines_left--;
            check_winning_condition(game);
        } else {
            printf("NO MINE IN SELECTED LOCATION");
        }
    }
}

void check_winning_condition(GameState* game) {
    if (game->mines_left == 0) {
        game->gameOver = true;
        printf("CONGRATS YOU WON!!!!");
    }
}

void search_tiles(GameState* game, int row, int column) {
    if (row >= 0 && column >= 0 && row < NUM_TILES_Y && column < NUM_TILES_X) {

        Tile* tile = &game->tiles[row][column];

        if (tile->revealed) {
            printf("ERROR: that tile is already revealed.");
        } else if (tile->is_mine) {
            tile->revealed = true;
            print_game_state(game);
            game_over(game);
        } else {
            reveal_tile(game, row, column);
        }
    }
}

void game_over(GameState* game) {
    game->gameOver = true;
    printf("YOU LOSE!");
}

void print_game_state(GameState* game) {
    printf("\n    ");
    for (int column = 1; column <= NUM_TILES_X; column++) {
        printf("%d ", column);
    }
    
    printf("\n----");
    for (int column = 0; column < NUM_TILES_X; column++) {
        printf("--");
    }

    for (int row = 0; row < NUM_TILES_Y; row++) {
        printf("\n%c | ", row + 'A');

        for (int column = 0; column < NUM_TILES_X; column++) {
            Tile* tile = &game->tiles[row][column];

            if (tile->revealed) {
                if (tile->is_mine) {
                    printf("* ");
                } else {
                    printf("%d ", tile->adjacent_mines);
                }
            } else if (tile->flagged) {
                printf("+ ");
            } else {
                printf("  ");
            }
        }
    }
    printf("\n");
}


int main() {
    GameState* game = malloc(sizeof(GameState));
    initialise_game(game);
    char row, option;
    int column;
    do {
        print_game_state(game);

        printf("Select an option:\n");
        printf("<R> Reveal tile:\n");
        printf("<P> Place flag:\n");
        printf("<Q> Quit Game:\n");
        scanf(" %c", &option);

        if (option == 'Q') {
            break;
        } else if (option == 'R') {
            printf("Please input a coordinate:\n");
            scanf(" %c%d", &row, &column);
            search_tiles(game, row - 'A', column - 1);
        } else if (option == 'P') {
            printf("Please input a coordinate:\n");
            scanf(" %c%d", &row, &column);
            place_flag(game, row - 'A', column - 1);
        } else {
            printf("INVALID INPUT");
        }

  
            

    } while (!game->gameOver);
    return 0;
}
