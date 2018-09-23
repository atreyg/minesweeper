#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define NUM_TILES_X 9
#define NUM_TILES_Y 9
#define NUM_MINES 10

typedef struct tile_struct
{
    int adjacent_mines;
    bool revealed;
    bool is_mine;
    bool flagged;
} Tile;

typedef struct game_struct
{
    int mines_left;
    bool gameOver;
    Tile tiles[NUM_TILES_X][NUM_TILES_Y];
} GameState;

void initialise_game(GameState *game);
void place_mines(GameState *game);
void increase_number_of_adjacent_mines(GameState *game, int row, int column);
void reveal_tile(GameState *game, int row, int column);
void place_flag(GameState *game, int row, int column);
void check_winning_condition(GameState *game);
void search_tiles(GameState *game, int row, int column);
void game_over(GameState *game);
void print_game_state(GameState *game);