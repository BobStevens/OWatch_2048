/*
 *  2048 Sliding Tile Game for the O Watch / TinyScreen+
 *
 *  Use the four O Watch buttons to slide the tiles horizontally or vertically. 
 *  Combine similar tiles to reach 2048 and beyond.
 *  
 *  Copyright (c) 2016 BobStevens
 *  
 *  This sliding tile game was inspired by the 2048 game by Gabriele Cirulli 
 *  https://github.com/gabrielecirulli/2048
 *      
 *  The MIT License (MIT)
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *  
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *  
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 *  
 */

// TinyScreen library
#include <TinyScreen.h> 
// the Arduino Zero's Real Time Clock library
#include <RTCZero.h> 

// bitmaps
#include "framebuffer_bitmaps.h"
#include "tile_bitmaps8.h"

// Board configuration
#define GRID_SIZE 4
#define GRID_LENGTH (GRID_SIZE * GRID_SIZE)
#define BOARD_ORIGIN_X 17
#define BOARD_ORIGIN_Y 1
#define TILE_WIDTH 15
#define TILE_SPACING 1
#define START_TILES 2
// index of the goal 2048 tile
#define WINNING_TILE 11
#define TEXT_COLOR 0xb7
// display the score for N seconds
#define SCORE_TIMER 2
// automatically turn off display after N seconds
#define AUTO_OFF 10
// animation fps
#define FRAMES_PER_SECOND 60
#define FRAME_DELAY_MS (1000/FRAMES_PER_SECOND)
// frame counter to display the merged tile reward graphic
#define MERGE_REWARD_FRAMES 10
// some bitmaps have an alpha channel color
#define ALPHA_CHANNEL 0x1c

// Cell structure
typedef struct {
  // grid coordinates on the board
  uint8_t x;
  uint8_t y;
  // animated x,y after a player move, for animation frames
  uint8_t animatedX;
  uint8_t animatedY;
  // index of this tile in the tiles[] bitmap array (0,2,4,8...)
  uint8_t index;
  // flag if merged during this move, tile can only be merged once per turn
  bool merged;
  // frame counter for a reward graphic on merged tiles
  uint8_t reward;
} cell_t;

// Player move vector
typedef struct {
  int8_t x;
  int8_t y;
} vector_t;

// Game states
typedef enum { 
  STATE_INITIAL, 
  STATE_NEWGAME, 
  STATE_PLAYER, 
  STATE_PREMERGE,
  STATE_MERGE,
  STATE_POSTMERGE,
  STATE_ADDTILES,
  STATE_CHECK,
  STATE_GAMEOVER,
  STATE_SLEEP,
  STATE_MENU,
  STATE_SETTIME,
  STATE_SETDATE,
  NUM_STATES 
}  state_t;


// Game properties
typedef struct {
  // create the board / grid of tiles
  cell_t grid[GRID_SIZE][GRID_SIZE];
  // game scores
  uint32_t score;
  uint32_t hiscore;
  bool won;
  // frame duration
  uint32_t animationframe_ms;
  // player move direction
  vector_t vector;
  // game state
  uint8_t state;
  uint8_t entering_state;
  uint8_t return_state;
  // track tile moves
  uint8_t tiles_moved;
  uint16_t tiles_moved_total;
  // track button presses
  uint16_t button_counter;
  // inactivity timeout tracker
  uint32_t activity_ms;
  // frame counter
  uint16_t frame_counter;
  // game start timestamp
  // uint32_t game_start_time;
} game_t;

// create the TinyScreen object
TinyScreen display = TinyScreen(TinyScreenPlus);
// Create the rtc object
RTCZero rtc;
byte seconds = 0;
byte minutes = 0;
byte hours = 0;
byte days = 0;
byte months = 1;
byte years = 0;

// create the global game struct
game_t game;

// initializes the game properties
void initGame() {
  newGame();
  game.hiscore = 0;
}

void newGame() {
  game.score = 0;
  game.won = false;
  game.animationframe_ms = millis() + FRAME_DELAY_MS;
  game.vector = {0,0};
  game.tiles_moved = 0;
  game.tiles_moved_total = 0;
  game.button_counter = 0;
  game.state = STATE_INITIAL;
  game.return_state = game.state;
  game.entering_state = true;
}

void setupTiles() {
  for ( uint8_t row = 0; row < GRID_SIZE; row++) {
    for ( uint8_t col = 0; col < GRID_SIZE; col++) {
      game.grid[col][row].index = 0;
      game.grid[col][row].x = BOARD_ORIGIN_X + (col * (TILE_SPACING + TILE_WIDTH));
      game.grid[col][row].y = BOARD_ORIGIN_Y + (row * (TILE_SPACING + TILE_WIDTH));
      game.grid[col][row].merged = false;
      game.grid[col][row].reward = 0;
      // set our destination equal to our current position
      game.grid[col][row].animatedX = game.grid[col][row].x;
      game.grid[col][row].animatedY = game.grid[col][row].y;
    }
  }
}

// redraw the board
bool displayBoard () {

  // copy background bitmap into framebuffer
  if (game.state == STATE_GAMEOVER) {
    memcpy( framebuffer, gameoverbackground, FRAMEBUFFER_LENGTH );
  } else {
    memcpy( framebuffer, background, FRAMEBUFFER_LENGTH );
  }
  // draw the tiles
  bool updating = false;
  for (uint8_t row = 0; row < GRID_SIZE; row++) {
    for (uint8_t col = 0; col < GRID_SIZE; col++) {
      // skip empty cells
      if (game.grid[col][row].index) {
        // if this tile has moved, animate it
        if (game.grid[col][row].x != game.grid[col][row].animatedX 
          || game.grid[col][row].y != game.grid[col][row].animatedY) {
            
          updating = true;
          game.grid[col][row].animatedX += game.vector.x;
          game.grid[col][row].animatedY += game.vector.y;

          // if tiles have completely merged, update the index value
          if (game.grid[col][row].merged 
            && game.grid[col][row].x == game.grid[col][row].animatedX 
            && game.grid[col][row].y == game.grid[col][row].animatedY) {
              
            // increment the tile value  
            game.grid[col][row].index ++;
            // reset the merged flag
            game.grid[col][row].merged = false;
            // set the reward flag, for one frame of a merged reward effect
            game.grid[col][row].reward = MERGE_REWARD_FRAMES;
            // update the score / hiscore
            game.score += pow(2,game.grid[col][row].index);
            if (game.score > game.hiscore) {
              game.hiscore = game.score;
            }
          } else if (game.grid[col][row].merged) {
            // draw the original tile that will be merged
            drawTile(game.grid[col][row]);
          }
          // draw the tile at its animation coordinates
          drawMovingTile(game.grid[col][row]);
        } else {
          // just draw the tile
          drawTile(game.grid[col][row]);
          // we'll need to draw the tile again, if currently displaying the reward graphic
          if (game.grid[col][row].reward > 0) {
            updating = true;
            // decrement merge reward frame counter
            game.grid[col][row].reward --;
          }
        }
      }
    }
  }
  // send framebuffer to tinyscreen
  drawBuffer();
  return updating;
}

// send the full framebuffer to tinyscreen
void drawBuffer() {
  display.setX(0,TinyScreen::xMax);
  display.setY(0,TinyScreen::yMax);
  display.startData();
  display.writeBuffer(framebuffer,FRAMEBUFFER_LENGTH);
  display.endTransfer();
}

// display the splash screen
void displaySplash() {
  // copy splash bitmap into framebuffer
  memcpy(framebuffer, splash, FRAMEBUFFER_LENGTH);
  // send framebuffer to tinyscreen
  drawBuffer();
}
// display the splash screen and time
void displaySplashTime() {
  // copy splash bitmap into framebuffer
  memcpy(framebuffer, splash, FRAMEBUFFER_LENGTH);
  // send framebuffer to tinyscreen
  drawBuffer();
  displayTime(40);
}

// display the time centered at Y
void displayTime(uint8_t y) {
  hours = rtc.getHours();
  minutes = rtc.getMinutes();
  seconds = rtc.getSeconds();
  // build up the time string, so we can use getPrintWidth()
  String time_str = String("");
  if (hours < 10) time_str.concat("0");
  time_str.concat(hours);
  time_str.concat(":");
  if (minutes < 10) time_str.concat("0");
  time_str.concat(minutes);
  char time_ch[9] = {};
  time_str.toCharArray(time_ch, time_str.length());
  
  display.setFont(liberationSansNarrow_16ptFontInfo);
  uint8_t x = ((TinyScreen::xMax - display.getPrintWidth(time_ch)) / 2) - 3;
  display.setCursor(x, y);
  display.print(time_str);
  // restore original font
  display.setFont(thinPixel7_10ptFontInfo); 
}

// display the menu screen
void displayMenu() {
  memset( framebuffer, 0, FRAMEBUFFER_LENGTH );
  // send framebuffer to tinyscreen
  drawBuffer();
  display.setCursor(0, 0);
  display.print("-----Options-----");
  display.setCursor(0, 10);
  display.print("< Info");
  display.setCursor(0, 45);
  display.print("< Exit");
  display.setCursor(45, 10);
  display.print("Set Time >");
  display.setCursor(43, 45);
  display.print("New Game >");
}

// display the set time options
void displaySetTime() {
  memset( framebuffer, 0, FRAMEBUFFER_LENGTH );
  // send framebuffer to tinyscreen
  drawBuffer();
  
  display.setCursor(0, 0);
  display.print("-----Set Time----");;
  display.setCursor(0, 10);
  display.print("< Set Date");
  display.setCursor(0, 45);
  display.print("< Exit");
  display.setCursor(61, 10);
  display.print("Hours >");
  display.setCursor(52, 45);
  display.print("Minutes >"); 

  displayTime(25);
}

// display the set date options
void displaySetDate() {
  memset( framebuffer, 0, FRAMEBUFFER_LENGTH );
  // send framebuffer to tinyscreen
  drawBuffer();
  
  display.setCursor(0, 0);
  display.print("-----Set Date----");
  display.setCursor(0, 10);
  display.print("< Year");
  display.setCursor(0, 45);
  display.print("< Exit");
  display.setCursor(61, 10);
  display.print("Month >");
  display.setCursor(71, 45);
  display.print("Day >"); 
  display.setCursor(15, 25);
  displayDate();
}

// display the time
void displayDate() {
  years = rtc.getYear();
  months = rtc.getMonth();
  days = rtc.getDay();
  
  display.setFont(liberationSansNarrow_16ptFontInfo);
  if (months < 10) display.print("0");
  display.print(months);
  display.print("-");
  if (days < 10) display.print("0");
  display.print(days);
  display.print("-");
  if (years < 10) display.print("0");
  display.print(years);
  display.setFont(thinPixel7_10ptFontInfo); 
}

// game over splash screen
void displayGameOver() {
  uint16_t index = 0;
  for (index = 0; index < FRAMEBUFFER_LENGTH; index++) {
    // ignore alpha channel 
    if (game.won) {
      if (success[index] != ALPHA_CHANNEL) {
        framebuffer[index] = success[index];
      }
    } else {
      if (gameover[index] != ALPHA_CHANNEL) {
        framebuffer[index] = gameover[index];
      }
    }
  }
  // send framebuffer to tinyscreen
  drawBuffer();
}

// game stats
void displayStats() {

  hours = rtc.getHours();
  minutes = rtc.getMinutes();
  seconds = rtc.getSeconds();
  
  memset(framebuffer, TS_8b_Black, FRAMEBUFFER_LENGTH);
  // send framebuffer to tinyscreen
  drawBuffer();
 
  display.setCursor(0, 0);
  display.print("Time: ");
  if (hours < 10) display.print("0");
  display.print(hours);
  display.print(":");
  if (minutes < 10) display.print("0");
  display.print(minutes);
  
  display.print(":");
  if (seconds < 10) display.print("0");
  display.print(seconds);
  
  display.setCursor(0, 10);
  display.print("Score: ");
  display.print(game.score);
  display.setCursor(0, 20);
  display.print("Moves: ");
  display.print(game.button_counter);
  display.setCursor(0, 30);
  display.print("Distance: ");
  display.print(game.tiles_moved_total);
  display.setCursor(0, 40);
  display.print("Level: ");
  display.print((uint16_t) pow(2, bestTile()));
  display.setCursor(0, 50);
  display.print("Hi-Score: ");
  display.print(game.hiscore);
}

// draw a merged or non-sliding tile
void drawTile(cell_t tile) {
  uint8_t y = 0;
  // draw the tile
  while(y < (TILE_WIDTH * TILE_WIDTH)){
    // convert the tile coordinates to the screen buffer location
    uint16_t index = ((TinyScreen::xMax + 1) * (y / TILE_WIDTH) ) + (y % TILE_WIDTH) + tile.x + (tile.y * (TinyScreen::xMax + 1)) ;
    if (index > 0 && index < ((TinyScreen::xMax + 1) * (TinyScreen::xMax + 1))) {
      framebuffer[index] = tiles[tile.index][y];
      if (tile.reward > 0) {
        // ignore alpha channel 0xff
        if (reward[y] != ALPHA_CHANNEL) {
          framebuffer[index] = reward[y];
        }
      }
    }
    y++;
  }
}
// draw a sliding tile
void drawMovingTile(cell_t tile) {
  uint8_t y = 0;
  // draw the tile
  while(y < (TILE_WIDTH * TILE_WIDTH)){
    // convert the tile coordinates to the screen buffer location
    uint16_t index = ((TinyScreen::xMax + 1) * (y / TILE_WIDTH) ) + (y % TILE_WIDTH) + tile.animatedX + (tile.animatedY * (TinyScreen::xMax + 1)) ;
    if (index > 0 && index < ((TinyScreen::xMax + 1) * (TinyScreen::xMax + 1))) {
      framebuffer[index] = tiles[tile.index][y];
    }
    y++;
  }
}

// Find the first available random position
cell_t* randomAvailableCell() {
  cell_t* opencells[GRID_LENGTH];
  uint8_t counter = 0;
  for ( uint8_t row = 0; row < GRID_SIZE; row++) {
    for ( uint8_t col = 0; col < GRID_SIZE; col++) {
      if (cellAvailable(game.grid[col][row])) {
        // cell is empty, save position, increment counter
        opencells[counter] = &game.grid[col][row];
        counter++;
      }
    }
  }
  // pick a random index, if no more cells return false
  if (counter) {
    return opencells[random(counter)];
  }
  return false;
}

// returns quantity of available cells
uint8_t availableCells() {
  uint8_t counter = 0;
  for ( uint8_t row = 0; row < GRID_SIZE; row++) {
    for ( uint8_t col = 0; col < GRID_SIZE; col++) {
      if (cellAvailable(game.grid[col][row])) {
        counter++;
      }
    }
  }
  return counter;
}

// Check if there are any cells available
bool cellsAvailable() {
  return !! availableCells();
}

// Check if the specified cell is free
bool cellAvailable(cell_t cell) {
  return ! cellOccupied(cell);
}

// Check if the specified cell is taken
bool cellOccupied(cell_t cell) {
  if (cell.index > 0) {
    return true;
  }
  return false;
}

// add the random tiles for a new game
void addStartTiles() {
  for (uint8_t i = 0; i < START_TILES; i++) {
    addRandomTile();
  }
}

// add a single tile in a random cell
void addRandomTile() {
  if (cellsAvailable()) {
    cell_t* tile = randomAvailableCell();
    tile->index = random(10) < 9 ? 1 : 2;
  }
}

// test all vectors for any valid moves or merges
bool movesAvailable() {
  vector_t vectors[4] = {{0,-1},{1,0},{-1,0},{0,1}};
  for (uint8_t i = 0; i<4; i++){
   
    // test each tile as needed
    for ( uint8_t row = 0; row < GRID_SIZE; row++) {
      for ( uint8_t col = 0; col < GRID_SIZE; col++) {
        // index of the cell we are moving
        uint8_t c = col;
        uint8_t r = row;
        // reverse the indexes based on direction of move
        if (vectors[i].x == 1) {
          // right
          c = GRID_SIZE - 1 - col;
        }
        if (vectors[i].y == 1) {
          // down
          r = GRID_SIZE - 1 - row;
        }
        // is this cell empty? 
        if (cellAvailable(game.grid[c][r])) {
          return true;
        }
  
        // skip edges
        // down
        if ( vectors[i].y == 1 && r == GRID_SIZE - 1) continue;
        // up
        if ( vectors[i].y == -1 && r == 0) continue;
        // right
        if ( vectors[i].x == 1 && c == GRID_SIZE - 1) continue;
        // left
        if ( vectors[i].x == -1 && c == 0) continue;
  
        // try to merge tile or move it in its row or column
        // LEFT / RIGHT
        if (vectors[i].y == 0) {
          for ( int8_t other = (c + vectors[i].x); other >= 0 && other < GRID_SIZE; other = other + vectors[i].x) {
            // if other tile is the same, merge it
            if (game.grid[other][r].index == game.grid[c][r].index && ! game.grid[other][r].merged && ! game.grid[c][r].merged) {
              return true;
            } else {
              break;
            }
            // if other cell is empty, move this tile into it
            if (cellAvailable(game.grid[other][r])) {
              return true;
            } else {
              break;
            }
          } // other tile loop
        } else {
          // UP / DOWN
          for ( int8_t other = (r + vectors[i].y); other >= 0 && other < GRID_SIZE; other = other + vectors[i].y) {
            // if other tile is the same, merge it
            if (game.grid[c][other].index == game.grid[c][r].index && ! game.grid[c][other].merged && ! game.grid[c][r].merged) {
              return true;
            } else {
              break;
            }
            // if other cell is empty, move this tile into it
            if (cellAvailable(game.grid[c][other])) {
              return true;
            } else {
              break;
            }
          } // other tile loop
        }
      } // row
    } // col
  }
  return false;
}

// returns highest tile value that exists on board
uint8_t bestTile() {
  uint8_t best_tile = 0;
  for ( uint8_t row = 0; row < GRID_SIZE; row++) {
    for ( uint8_t col = 0; col < GRID_SIZE; col++) {
      if (game.grid[col][row].index > best_tile) {
        best_tile = game.grid[col][row].index;
      }
    }
  }
  return best_tile;
}

// returns true if 2048 tile exists on board
uint8_t wonTheGame() {
  if (bestTile() == WINNING_TILE) {
    return true;
  }
  return false;
}

// swap a tile into an adjacent empty cell
void swapIntoEmptyCell(cell_t *cell1, cell_t *cell2) {
  // swap tile into an empty cell
  cell2->animatedX = cell1->animatedX;
  cell2->animatedY = cell1->animatedY;
  cell2->index = cell1->index;
  cell2->merged = cell1->merged;

  // set this cell to empty unmerged cell
  cell1->index = 0;
  cell1->animatedX = cell1->x;
  cell1->animatedY = cell1->y;
  cell1->merged = false;    
}

// merge a tile into a same value cell
void mergeWithSameCell(cell_t *cell1, cell_t *cell2) {
  // merge it, but don't update index value of tile until animation is complete
  cell2->merged = true;
  // set animated x,y to this tiles x,y
  cell2->animatedX = cell1->animatedX;
  cell2->animatedY = cell1->animatedY;
  // set this tile to empty unmerged cell
  cell1->index = 0;
  cell1->animatedX = cell1->x;
  cell1->animatedY = cell1->y;
  cell1->merged = false;   
}

/* process the button press, update the tiles
 * move each tile as far as possible in the direction chosen by the player
 * tiles can only be merged once per turn
 * returns count of tiles moved or merged
 *  LT row 0-3 col 0-3 x=-1 y= 0
 *  RT row 0-3 col 3-0 x=+1 y= 0
 *  UP row 0-3 col 0-3 x= 0 y=-1
 *  DN row 3-0 col 0-3 x= 0 y=+1
*/
uint16_t processButtonMove() {
  return processButton(false);
}
uint16_t processButtonMerge() {
  return processButton(true);
}
uint16_t processButton(bool merging) {
  uint16_t counter = 0;
  
  // ignore invalid vectors
  if (!checkVector()) return 0;
  
  // move each tile as needed
  for ( uint8_t row = 0; row < GRID_SIZE; row++) {
    for ( uint8_t col = 0; col < GRID_SIZE; col++) {
      // index of the cell we are moving
      uint8_t c = col;
      uint8_t r = row;
      // reverse the indexes based on direction of move
      if (game.vector.x == 1) {
        // right
        c = GRID_SIZE - 1 - col;
      }
      if (game.vector.y == 1) {
        // down
        r = GRID_SIZE - 1 - row;
      }
      // is this cell empty? skip it
      if (cellAvailable(game.grid[c][r])) {
        continue;
      }

      // skip edges
      // down
      if ( game.vector.y == 1 && r == GRID_SIZE - 1) continue;
      // up
      if ( game.vector.y == -1 && r == 0) continue;
      // right
      if ( game.vector.x == 1 && c == GRID_SIZE - 1) continue;
      // left
      if ( game.vector.x == -1 && c == 0) continue;

      // try to merge tile or move it as far as possible in its row or column
      // LEFT / RIGHT
      if (game.vector.y == 0) {
        for ( int8_t other = (c + game.vector.x); other >= 0 && other < GRID_SIZE; other = other + game.vector.x) {
          if (merging) {
            // if other tile is the same, merge it
            if (game.grid[other][r].index == game.grid[c][r].index && ! game.grid[other][r].merged && ! game.grid[c][r].merged) {
              mergeWithSameCell(&game.grid[c][r], &game.grid[other][r]);
              // we merged: set c to other, so we can make more progress
              c = other;
              counter ++;
            } else {
              // we can't go any further
              break;
            }
          }else{
            // if other cell is empty, move this tile into it
            if (cellAvailable(game.grid[other][r])) {
              // swap values of these cells
              swapIntoEmptyCell(&game.grid[c][r], &game.grid[other][r]);
              // we moved: set c to other, so we can make more progress
              c = other;
              counter ++;
            } else {
              // we can't go any further
              break;
            }
          }
        } // other tile loop
      } else {
        // UP / DOWN
        for ( int8_t other = (r + game.vector.y); other >= 0 && other < GRID_SIZE; other = other + game.vector.y) {
          if (merging) {
            // if other tile is the same, merge it
            if (game.grid[c][other].index == game.grid[c][r].index && ! game.grid[c][other].merged && ! game.grid[c][r].merged) {
              mergeWithSameCell(&game.grid[c][r], &game.grid[c][other]);
              // we merged: set r to other, so we can make more progress
              r = other;
              counter ++;
            } else {
              // we can't go any further
              break;
            }
          }else{
            // if other cell is empty, move this tile into it
            if (cellAvailable(game.grid[c][other])) {
              // swap values of these cells
              swapIntoEmptyCell(&game.grid[c][r], &game.grid[c][other]);
              // we moved: set r to other, so we can make more progress
              r = other;
              counter ++;
            } else {
              // we can't go any further
              break;
            }
          }
        } // other tile loop
      }
    } // row
  } // col
  return counter;
}

// ignore invalid / diagonal vectors
bool checkVector() {
  if (game.vector.x == 0 && game.vector.y == 0) return false;
  if (game.vector.x != 0 && game.vector.y != 0) return false;
  return true;
}

// check for inactivity, turn off display, button press turns it back on
void inactivityChecker() {
  if ((millis() - game.activity_ms) > (AUTO_OFF * 1000)) {
    // going to sleep, remember where to go when we wake up
    if (game.state != STATE_SLEEP) {
      // don't return to menu from sleep
      if (game.state < STATE_SLEEP) {
        game.return_state = game.state;
      }
      game.state = STATE_SLEEP;
      game.entering_state = true;
    }
  }
}

// wait for button release
void waitForButtonRelease()  {
  while (display.getButtons()) {}
  // debounce delay
  delay(10);
}

void setBrightness() {
  uint8_t brightness = 15;
  uint8_t hours = rtc.getHours();
  // set brightness
  if (hours <= 12)
    brightness = hours + 3; // 0 hours = 3 brightness, noon = 15
  else if (hours >= 18)
    brightness = (24 - hours) * 2 + 2;  // 23 hours = 4 brightness, 18 hours = 14
  else
    brightness = 15; // full brightness all afternoon
  if (brightness < 3)
    brightness = 3;
  if (brightness > 15)
    brightness = 15;
  display.setBrightness(brightness);       
}

void setup() {
  char s_month[5];
  int tmonth, tday, tyear, thour, tminute, tsecond;
  static const char month_names[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  
  display.begin();                              // Initializes TinyScreen board
  display.setFlip(1);                           // Flips the TinyScreen rightside up for O Watch
  display.setColorMode(TSColorModeRGB);         // switch from GBR to RGB
  display.on();                                 // Turns TinyScreen display on
  display.fontColor(TEXT_COLOR, TS_8b_Black);   // Set the font color, font background
  display.setFont(thinPixel7_10ptFontInfo);     // Set the font type

  // initialize the random number generator
  randomSeed(analogRead(PIN_A1) * analogRead(PIN_A2));
  
  // initialize RTC
  rtc.begin(); 
  sscanf(__DATE__, "%s %d %d", s_month, &tday, &tyear);
  sscanf(__TIME__, "%d:%d:%d", &thour, &tminute, &tsecond);
  // Find the position of this month's string inside month_names, do a little
  // pointer subtraction arithmetic to get the offset, and divide the
  // result by 3 since the month names are 3 chars long.
  tmonth = (strstr(month_names, s_month) - month_names) / 3;
  months = tmonth + 1;  // The RTC library expects months to be 1 - 12.
  days = tday;
  years = tyear - 2000; // The RTC library expects years to be from 2000.
  hours = thour;
  minutes = tminute;
  seconds = tsecond;
  rtc.setTime(hours, minutes, seconds);
  rtc.setDate(days, months, years);
        
  initGame();  
}

void loop() {
  uint8_t button = 0;
  
  switch(game.state) {
    case STATE_INITIAL:
      // Entering state
      if(game.entering_state) {
        setBrightness();
        game.activity_ms = millis();
        game.entering_state = false;
        displaySplash(); 
      }

      // Exiting state
      button = display.getButtons();
      // process the button press
      if (button) {
        switch (button) {
          case TSButtonUpperLeft:
            break;
          case TSButtonUpperRight:
            break;
          case TSButtonLowerLeft:
            game.state = STATE_MENU;
            game.entering_state = true;
            waitForButtonRelease();
            break;
          case TSButtonLowerRight:
            game.state = STATE_NEWGAME;
            game.entering_state = true;
            waitForButtonRelease();
            break;
        }
      }
      
      inactivityChecker();
      break;

    case STATE_NEWGAME:
      // Entering state
      if(game.entering_state) {
        game.activity_ms = millis();
        game.entering_state = false;
        newGame();  
        // setup the tiles
        setupTiles();
        addStartTiles();
      }
      // Updating state
      if (!displayBoard()){
        // Exiting state
        game.state = STATE_PLAYER;
        game.entering_state = true;
      }
      break;

    case  STATE_PLAYER:
      // Entering state
      if(game.entering_state) {
        game.activity_ms = millis();
        game.entering_state = false;
        // reset player move vector
        game.vector = {0, 0};
        displayBoard();
      }

      // Updating state
      button = display.getButtons();
      // process the button press
      if (button) {
        switch (button) {
          case TSButtonUpperLeft:
            // up
            game.vector = {0,-1};
            break;
          case TSButtonUpperRight:
            // right
            game.vector = {1,0};
            break;
          case TSButtonLowerLeft:
            // left
            game.vector = {-1,0};
            break;
          case TSButtonLowerRight:
            // down
            game.vector = {0,1};
            break;
        }
      }
      // wait until button is released
      if (!button && (game.vector.x != 0 || game.vector.y != 0)) {
        // Exiting state
        game.state = STATE_PREMERGE;
        game.entering_state = true;  
      }
      
      inactivityChecker();
      break;

    case STATE_PREMERGE:
      // Entering state
      if(game.entering_state) {
        game.animationframe_ms = millis() + FRAME_DELAY_MS;
        game.activity_ms = millis();
        game.entering_state = false;
        // reset tile move counter
        game.tiles_moved = 0;
        // Move tiles into empty spaces
        game.tiles_moved += processButtonMove();
      }
      if (millis() > game.animationframe_ms) {
        game.animationframe_ms = millis() + FRAME_DELAY_MS;
        if (!displayBoard()) {
          game.state = STATE_MERGE;
          game.entering_state = true;  
        }
      }
      break;

    case STATE_MERGE:
      // Entering state
      if(game.entering_state) {
        game.animationframe_ms = millis() + FRAME_DELAY_MS;
        game.entering_state = false;
        // Merge tiles
        game.tiles_moved += processButtonMerge();
      }

      if (millis() > game.animationframe_ms) {
        game.animationframe_ms = millis() + FRAME_DELAY_MS;
        if (!displayBoard()) {
          game.state = STATE_POSTMERGE;
          game.entering_state = true;  
        }
      }
      break;

    case STATE_POSTMERGE:
      // Entering state
      if(game.entering_state) {
        game.animationframe_ms = millis() + FRAME_DELAY_MS;
        game.entering_state = false;
        // Move tiles into empty spaces again
        game.tiles_moved += processButtonMove();
      }

      if (millis() > game.animationframe_ms) {
        game.animationframe_ms = millis() + FRAME_DELAY_MS;
        if (!displayBoard()) {
          // if tiles moved or merged, then add a new random tile
          if (game.tiles_moved) {
            game.button_counter ++;
            game.tiles_moved_total += game.tiles_moved;
            game.state = STATE_ADDTILES;
          } else {
            game.state = STATE_CHECK;
          }
          game.entering_state = true;  
        }
      }
      break;

    case STATE_ADDTILES:
      // Entering state
      if(game.entering_state) {
        game.animationframe_ms = millis() + FRAME_DELAY_MS;
        game.frame_counter = FRAMES_PER_SECOND / 10;
        game.entering_state = false;
      }
      
      if (millis() > game.animationframe_ms) {
        // reset the frame timer
        game.animationframe_ms = millis() + FRAME_DELAY_MS;
        // decrement the frame counter
        if (game.frame_counter > 0) { 
          game.frame_counter --;
          if (game.frame_counter == 0) { 
            // add the next tile
            addRandomTile();
          }
        }
        if (game.frame_counter == 0) { 
          if (!displayBoard()) {
            game.state = STATE_CHECK;
            game.entering_state = true;  
          }
        }
      }
      break;

    case STATE_CHECK:
      // game over?
      // check for 2048 tile
      if (!game.won && wonTheGame()) {
        game.won = true;
        game.state = STATE_GAMEOVER;
        game.entering_state = true;  
      } else if (!cellsAvailable() && !movesAvailable()) {
        game.state = STATE_GAMEOVER;
        game.entering_state = true;  
      } else {
        game.state = STATE_PLAYER;
        game.entering_state = true;  
      }
      break;
      
    case STATE_GAMEOVER:
      // Entering state
      if(game.entering_state) {
        game.entering_state = false;
        game.activity_ms = millis();
        game.animationframe_ms = millis() + FRAME_DELAY_MS;
        game.frame_counter = 2 * SCORE_TIMER * FRAMES_PER_SECOND;
        displayGameOver();
      }
      
      if (millis() > game.animationframe_ms) {
        // reset the frame timer
        game.animationframe_ms = millis() + FRAME_DELAY_MS;
        // decrement the frame counter
        if (game.frame_counter > 0) { 
          game.frame_counter --;
          if (game.frame_counter == 0) { 
            displayBoard(); 
          }
        }
      }
      
      // buttons only function when splash screen is gone
      if (game.frame_counter == 0) { 
        // Updating state
        button = display.getButtons();
        if (button) {
          switch (button) {
            case TSButtonUpperLeft:
              displayStats(); 
              break;
            case TSButtonUpperRight:
              break;
            case TSButtonLowerLeft:
              break;
            case TSButtonLowerRight:
              break;
          }
        }
         
        // Exiting state
        // continue past 2048 or start a new game
        if (button) {
          switch (button) {
            case TSButtonUpperLeft:
              break;
            case TSButtonUpperRight:
              break;
            case TSButtonLowerLeft:
              game.state = STATE_NEWGAME;
              game.entering_state = true;
              break;
            case TSButtonLowerRight:
              if (game.won) {
                // allow playing beyond 2048
                game.state = STATE_PLAYER;
              } else {
                game.state = STATE_NEWGAME;
              }
              game.entering_state = true;
              break;
          }
          
          waitForButtonRelease();
        }
      }
      inactivityChecker();
      break;

    case STATE_SLEEP:
      // Entering state
      if(game.entering_state) {
        game.animationframe_ms = millis() + FRAME_DELAY_MS;
        game.frame_counter = FRAMES_PER_SECOND;
        game.entering_state = false;
        display.off();
      }   
      
      // Exiting state
      button = display.getButtons();
      if (button) {
        setBrightness();
        display.on();
        
        switch (button) {
          case TSButtonUpperLeft:
            // update the display clock while the button is pressed
            while (display.getButtons()) {
              if (game.frame_counter == FRAMES_PER_SECOND) {
                  displayStats(); 
              }
              // next frame
              if (millis() > game.animationframe_ms) {
                // reset the frame timer
                game.animationframe_ms = millis() + FRAME_DELAY_MS;
                // decrement the frame counter
                if (game.frame_counter > 0) game.frame_counter --;
                if (game.frame_counter == 0) game.frame_counter = FRAMES_PER_SECOND;
              }
            }
            game.state = game.return_state;
            game.entering_state = true;
            break;
          case TSButtonUpperRight:
            // update the display clock while the button is pressed
            while (display.getButtons()) {
              if (game.frame_counter == FRAMES_PER_SECOND) {
                  displaySplashTime(); 
              }
              // next frame
              if (millis() > game.animationframe_ms) {
                // reset the frame timer
                game.animationframe_ms = millis() + FRAME_DELAY_MS;
                // decrement the frame counter
                if (game.frame_counter > 0) game.frame_counter --;
                if (game.frame_counter == 0) game.frame_counter = FRAMES_PER_SECOND;
              }
            }
            game.state = game.return_state;
            game.entering_state = true;
            break;
          case TSButtonLowerLeft:
            displaySplash(); 
            game.state = STATE_MENU;
            game.entering_state = true;
            waitForButtonRelease();
            break;
          case TSButtonLowerRight:
            displaySplash(); 
            game.state = game.return_state;
            game.entering_state = true;
            waitForButtonRelease();
            break;
        }
      }
      break;

    case STATE_MENU:
      // Entering state
      if(game.entering_state) {
        game.activity_ms = millis();
        game.entering_state = false;
        displayMenu();
      } 
        
      // Updating state
      button = display.getButtons();
      if (button) {
        switch (button) {
          case TSButtonUpperLeft:
            game.activity_ms = millis();
            displayStats(); 
            break;
          case TSButtonUpperRight:
            break;
          case TSButtonLowerLeft:
            game.state = STATE_MENU;
            game.entering_state = true;
            waitForButtonRelease(); 
            break;
          case TSButtonLowerRight:
            break;
        }
      }
      
      // Exiting state
      button = display.getButtons();
      if (button) {
        switch (button) {
          case TSButtonUpperLeft:
            break;
          case TSButtonUpperRight:
            game.state = STATE_SETTIME;
            game.entering_state = true;
            waitForButtonRelease();
            break;
          case TSButtonLowerLeft:
            // return to previous state if possible
            if (game.return_state != game.state){
              game.state = game.return_state;
              game.entering_state = true;
            } else {
              game.state = STATE_INITIAL;
              game.entering_state = true;
            }
            waitForButtonRelease();
            break;
          case TSButtonLowerRight:
            game.state = STATE_NEWGAME;
            game.entering_state = true;
            waitForButtonRelease();
            break;
        }
      }
      
      // Updating state
      inactivityChecker();
      break;

    case STATE_SETTIME:
      // Entering state
      if(game.entering_state) {
        game.animationframe_ms = millis() + FRAME_DELAY_MS;
        game.frame_counter = FRAMES_PER_SECOND;
        game.activity_ms = millis();
        game.entering_state = false;
        displaySetTime();
      } 
        
      // Updating state
      button = display.getButtons();
      if (button) {
        game.activity_ms = millis();

        switch (button) {
          case TSButtonUpperLeft:
            break;
          case TSButtonUpperRight:
            hours = rtc.getHours();
            hours ++;
            if (hours > 23) hours = 0;
            rtc.setTime(hours, rtc.getMinutes(), rtc.getSeconds());
            displaySetTime();
            break;
          case TSButtonLowerLeft:
            break;
          case TSButtonLowerRight:
            minutes = rtc.getMinutes();
            minutes ++;
            if (minutes > 59) minutes = 0;
            rtc.setTime(rtc.getHours(), minutes, rtc.getSeconds());
            displaySetTime();
            break;
        }
        
      }
      // next frame
      if (millis() > game.animationframe_ms) {
        // reset the frame timer
        game.animationframe_ms = millis() + FRAME_DELAY_MS;
        // decrement the frame counter
        if (game.frame_counter > 0) { 
          game.frame_counter --;
        }
        if (game.frame_counter == 0) { 
          displaySetTime();
          game.frame_counter = FRAMES_PER_SECOND;
        } 
      }
      
      // Exiting state
      if (button) {
        switch (button) {
          case TSButtonUpperLeft:
            game.entering_state = true;
            game.state = STATE_SETDATE;
            break;
          case TSButtonLowerLeft:
            game.entering_state = true;
            // return to previous state if possible
            if (game.return_state != game.state){
              game.state = game.return_state;
            } else {
              game.state = STATE_MENU;
            }
            break;
        }

        waitForButtonRelease();
      } 
      
      inactivityChecker();
      break;

    case STATE_SETDATE:
      // Entering state
      if(game.entering_state) {
        game.animationframe_ms = millis() + FRAME_DELAY_MS;
        game.frame_counter = FRAMES_PER_SECOND;
        game.activity_ms = millis();
        game.entering_state = false;
        displaySetDate();
      } 
        
      // Updating state
      button = display.getButtons();
      if (button) {
        game.activity_ms = millis();
        
        switch (button) {
          case TSButtonUpperLeft:
            years = rtc.getYear();
            years ++;
            if (years > 99) years = 0;
            rtc.setDate(rtc.getDay(), rtc.getMonth(), years);
            displaySetDate();
            break;
          case TSButtonUpperRight:
            months = rtc.getMonth();
            months ++;
            if (months > 12) months = 1;
            rtc.setDate(rtc.getDay(), months, rtc.getYear());
            displaySetDate();
            break;
          case TSButtonLowerLeft:
            break;
          case TSButtonLowerRight:
            days = rtc.getDay();
            days ++;
            if (days > 31) days = 1;
            rtc.setDate(days, rtc.getMonth(), rtc.getYear());
            displaySetDate();
            break;
        }
      }
      
      // next frame
      if (millis() > game.animationframe_ms) {
        // reset the frame timer
        game.animationframe_ms = millis() + FRAME_DELAY_MS;
        // decrement the frame counter
        if (game.frame_counter > 0) { 
          game.frame_counter --;
        }
        if (game.frame_counter == 0) { 
          displaySetDate();
          game.frame_counter = FRAMES_PER_SECOND;
        } 
      }
      
      // Exiting state
      if (button) {
        if (button == TSButtonLowerLeft) {
          game.entering_state = true;
          // return to previous state if possible
          if (game.return_state != game.state){
            game.state = game.return_state;
          } else {
            game.state = STATE_MENU;
          }
        } 
        waitForButtonRelease();
      } 
      
      inactivityChecker();
      break;
  } 
}
