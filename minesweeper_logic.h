#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define NUM_TILES_X 9
#define NUM_TILES_Y 9
#define NUM_MINES 10

typedef struct tile_struct {
    int adjacent_mines;
    bool revealed;
    bool is_mine;
    bool flagged;
} Tile;

typedef struct game_struct {
    int mines_left;
    Tile tiles[NUM_TILES_X][NUM_TILES_Y];
} GameState;

void initialise_game(GameState *game);
void place_mines(GameState *game);
void increase_number_of_adjacent_mines(GameState *game, int row, int column);
void reveal_tile(GameState *game, int row, int column);
int place_flag(GameState *game, int row, int column);
int search_tiles(GameState *game, int row, int column);
void print_game_state(GameState *game);
void update_end_board(GameState *game, int state);