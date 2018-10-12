#include "minesweeper_logic.h"
#include <pthread.h>
#include "common_constants.h"

// Mutex used to control synchronise use of the rand() function
pthread_mutex_t rand_mutex;

// Resets game field for a new gamew
void initialise_game(GameState *game) {
    game->mines_left = NUM_MINES;
    for (int row = 0; row < NUM_TILES_Y; row++) {
        for (int column = 0; column < NUM_TILES_X; column++) {
            Tile *tile = &game->tiles[row][column];
            tile->adjacent_mines = 0;
            tile->revealed = false;
            tile->is_mine = false;
            tile->flagged = false;
        }
    }

    // lock mutex to only allow one thread to use the rand() function at a time
    pthread_mutex_lock(&rand_mutex);
    place_mines(game);
    pthread_mutex_unlock(&rand_mutex);
}

// Place mines in random spots on the game board
void place_mines(GameState *game) {
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

// Increases the count of adjacent mines on all tiles surrounding a tile
// containing a mine
void increase_number_of_adjacent_mines(GameState *game, int row, int column) {
    // loop over tiles surrounding the mine
    for (int i = -1; i <= 1; i++) {
        for (int j = -1; j <= 1; j++) {
            // ensure the given coordinate is valid (i.e. not off the playfield)
            if (row + i >= 0 && column + j >= 0 && row + i < NUM_TILES_Y &&
                column + j < NUM_TILES_X) {
                // increment mine count of surrounding tiles
                game->tiles[row + i][column + j].adjacent_mines++;
            }
        }
    }
}

// change a tile's game state to be revealed
void reveal_tile(GameState *game, int row, int column) {
    // ensure the specified tile is a valid coordinate (on the game board)
    if (row >= 0 && column >= 0 && row < NUM_TILES_Y && column < NUM_TILES_X) {
        // reveal the tile if not already revealed
        Tile *tile = &game->tiles[row][column];
        if (tile->revealed) {
            return;
        }
        tile->revealed = true;

        // if the tile has no adjacent mines, recursively call the function on
        // all surrounding tiles
        if (tile->adjacent_mines == 0) {
            for (int i = -1; i <= 1; i++) {
                for (int j = -1; j <= 1; j++) {
                    reveal_tile(game, row + i, column + j);
                }
            }
        }
    }
}

// Places a flag on a specified tile
int place_flag(GameState *game, int row, int column) {
    // ensure the coordinate is valid (on the board)
    if (row >= 0 && column >= 0 && row < NUM_TILES_Y && column < NUM_TILES_X) {
        Tile *tile = &game->tiles[row][column];

        // flag the tile if it is a mine and decrement the number of remaining
        // mines
        if (tile->is_mine) {
            tile->flagged = true;
            game->mines_left--;

            if (game->mines_left == 0) {
                update_end_board(game, GAME_WON);
                return GAME_WON;
            }

            return NORMAL;
        } else {
            // otherwise return that no mine was found
            return NO_MINE_AT_FLAG;
        }
    }

    return INVALID_COORDINATES;
}

void update_end_board(GameState *game, int state) {
    for (int row = 0; row < NUM_TILES_Y; row++) {
        for (int column = 0; column < NUM_TILES_X; column++) {
            Tile *tile = &game->tiles[row][column];

            if (tile->is_mine) {
                tile->revealed = true;
            } else if (state == GAME_WON) {
                tile->revealed = true;
            } else if (state == GAME_LOST) {
                tile->revealed = false;
            }
        }
    }
}

// Handles logic of revealing a specified tile
int search_tiles(GameState *game, int row, int column) {
    // check that the coordinate is valid
    if (row >= 0 && column >= 0 && row < NUM_TILES_Y && column < NUM_TILES_X) {
        Tile *tile = &game->tiles[row][column];

        // check state of mine and take appropriate action
        if (tile->revealed) {
            return TILE_ALREADY_REVEALED;
        } else if (tile->is_mine) {
            tile->revealed = true;
            update_end_board(game, GAME_LOST);
            return GAME_LOST;
        } else {
            reveal_tile(game, row, column);
        }
        return NORMAL;
    }
    return INVALID_COORDINATES;
}

void print_game_state(GameState *game) {
    printf("\nRemaining mines: %d\n", game->mines_left);

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
            Tile *tile = &game->tiles[row][column];

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