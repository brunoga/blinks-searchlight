#include <blinklib.h>

#define GAME_STATE_IDLE 0
#define GAME_STATE_PLAY 1
#define GAME_STATE_END_WIN 2
#define GAME_STATE_END_LOSE 3

#define PLAY_TIME_IN_SECONDS 60
#define FADE_TO_BLACK_TIME_IN_MS 500

#define LIGHT_COLOR WHITE
#define BAT_COLOR MAGENTA
#define FLOOR_COLOR YELLOW
#define WIN_COLOR BLUE
#define LOSE_COLOR RED

// Uncomment this to enable debug features:
//
// - Currently only always show the bat.
//#define DEBUG

typedef byte GameState;
static GameState game_state_;

union FaceState {
  struct {
    byte game_state : 2;
    bool send_light : 1;
    bool is_lit : 1;
    bool send_bat : 1;
    bool has_bat : 1;
    byte reserved : 2;
  };

  byte as_byte;
};
static FaceState in_face_state_[FACE_COUNT];
static FaceState out_face_state_[FACE_COUNT];

struct BlinkState {
  bool is_player;
  bool has_bat;
  bool time_tracker;
  bool game_started;
  byte move_bat_face;
};
static BlinkState blink_state_;

static const byte opposite_face_[] = {3, 4, 5, 0, 1, 2};

static Timer game_play_timer_;
static Timer fade_to_black_timer_;

static void set_game_state(byte game_state) { game_state_ = game_state; }

static byte get_game_state() {
  // Return our current game state.
  return game_state_;
}

static bool is_lit() {
  if (blink_state_.is_player) {
    // This is a player Blink. Always lit.
    return true;
  }

  FOREACH_FACE(face) {
    if (in_face_state_[face].send_light) {
      // Some neighbor is shinning a light on us. So we are lit.
      return true;
    }
  }

  // We are not lit.
  return false;
}

void setup() {
  randomize();

  blink_state_.move_bat_face = FACE_COUNT;
}

static void move_bat() {
  // Keep track of the number of possible moves and the faces the bat can
  // move through.
  byte possible_move_count = 0;
  int8_t possible_move_face[FACE_COUNT];

  FOREACH_FACE(face) {
    if (!isValueReceivedOnFaceExpired(face) && !in_face_state_[face].is_lit) {
      // Face is not expired and is not lit. Bat can move through there.
      possible_move_face[possible_move_count] = face;
      possible_move_count++;
    }
  }

  if (possible_move_count == 0) {
    // The bat can not be moved anywhere. Game over.
    set_game_state(GAME_STATE_END_WIN);

    return;
  }

  // Pick a random face from the ones we can move to.
  byte move_bat_face = possible_move_face[random(possible_move_count - 1)];

  // Send the bat through there.
  out_face_state_[move_bat_face].send_bat = true;

  // And remember the face.
  blink_state_.move_bat_face = move_bat_face;
}

static void render_blink_state() {
  switch (get_game_state()) {
    case GAME_STATE_IDLE:
      // When idle, just render the Blink as the floor.
      //
      // TODO(bga): It might be nice to animate the "flashlight" face as if it
      // was searching for something.
      setColor(FLOOR_COLOR);
      break;
    case GAME_STATE_PLAY:
      // Play state. Things are a bit more complex now.
      if (is_lit()) {
        // This Blink is lit.
        if (blink_state_.is_player) {
          // It is a player Blink. Reset all faces to off.
          setColor(OFF);

          // And set face 0 to white.
          setColorOnFace(WHITE, 0);
        } else if (blink_state_.has_bat) {
          // It has the bat and it was caught by a flashlight. Render it as the
          // bat.
          setColor(BAT_COLOR);
        } else {
          // Just set the color to white to show we are lit.
          setColor(LIGHT_COLOR);
        }
      } else if (blink_state_.has_bat) {
#ifdef DEBUG
        setColor(BAT_COLOR);
#else
        // This Blink is not lit and has the bat. Render it according to the
        // fade to black timer (i.e. it will start at the given color and fade
        // to off).
        setColor(dim(BAT_COLOR,
                     (MAX_BRIGHTNESS * fade_to_black_timer_.getRemaining()) /
                         FADE_TO_BLACK_TIME_IN_MS));
#endif
      } else {
        // Not lit and no bat. Render it according to the fade to black timer
        // (i.e. it will start at the given color and fade to off).
        setColor(dim(FLOOR_COLOR,
                     (MAX_BRIGHTNESS * fade_to_black_timer_.getRemaining()) /
                         FADE_TO_BLACK_TIME_IN_MS));
      }
      break;
    case GAME_STATE_END_WIN:
      // We won!
      if (blink_state_.has_bat) {
        // If we have the bat, render it so all players can see where it
        // was.
        setColor(BAT_COLOR);
      } else {
        // No bat. Render as the win color.
        setColor(WIN_COLOR);
      }
      break;
    case GAME_STATE_END_LOSE:
      // We lost.
      if (blink_state_.has_bat) {
        // If we have the bat, render it so all players can see where it
        // was.
        setColor(BAT_COLOR);
      } else {
        // No bat. Render as the lose color.
        setColor(LOSE_COLOR);
      }
      break;
  }

  // Update reporting state for all faces.
  FOREACH_FACE(face) {
    out_face_state_[face].game_state = get_game_state();
    out_face_state_[face].has_bat = blink_state_.has_bat;
    out_face_state_[face].is_lit = is_lit();

    setValueSentOnFace(out_face_state_[face].as_byte, face);
  }
}

static void idle_loop() {
  // Reset our state.
  blink_state_.has_bat = false;
  blink_state_.is_player = false;
  blink_state_.time_tracker = false;
  blink_state_.game_started = true;
  blink_state_.move_bat_face = FACE_COUNT;

  if (buttonSingleClicked() && !hasWoken()) {
    // The button was single-clicked and it was not a wake-up click.
    if (isAlone()) {
      // We are alone, so we are a player Blink now.
      blink_state_.is_player = true;
    } else {
      // We are not alone, so we are a piece of the board that has the bat.
      blink_state_.has_bat = true;

      // And we will also keep track of the timer.
      game_play_timer_.set(1000L * PLAY_TIME_IN_SECONDS);
      blink_state_.time_tracker = true;
    }

    // Switch to the play state.
    set_game_state(GAME_STATE_PLAY);
  }
}

static void play_player_loop() {
  // We are a player piece, so we always send light on our face 0.
  out_face_state_[0].send_light = true;

  // And we tell Blinks in any face that we are lit.
  FOREACH_FACE(face) { out_face_state_[0].is_lit = true; }
}

static void play_board_loop() {
  if (blink_state_.game_started) {
    // Game just started. enable fade to black animation.
    fade_to_black_timer_.set(FADE_TO_BLACK_TIME_IN_MS);

    // And remember we already started.
    blink_state_.game_started = false;
  }

  if (blink_state_.has_bat && blink_state_.move_bat_face == FACE_COUNT) {
    // We hold the bat and we are not transferring it yet. Check if
    // whatever Blink that send it to us still thinks it holds it.
    FOREACH_FACE(face) {
      if (in_face_state_[face].has_bat) {
        // It does. Do not try to move it to another Blink yet.
        return;
      }
    }

    // Move bat to another Blink.
    move_bat();
  }
}

static void play_loop() {
  if (blink_state_.is_player) {
    // We are a player (flashlight) blink.
    play_player_loop();
  } else {
    // We are a piece of the board.
    play_board_loop();
  }

  if (blink_state_.time_tracker && game_play_timer_.isExpired()) {
    // We are tracking the time and the play timer expired. Game over.
    set_game_state(GAME_STATE_END_LOSE);
  }
}

static void end_loop() {
  if (buttonSingleClicked() && !hasWoken()) {
    // Button was clicked and it was not to wake the Blink up. Switch to idle
    // state.
    set_game_state(GAME_STATE_IDLE);
  }
}

static void process_neighbors() {
  FOREACH_FACE(face) {
    FaceState in_face_state = {.as_byte = 0};

    if (!isValueReceivedOnFaceExpired(face)) {
      // We have a valid value on this face. Parse it.
      in_face_state.as_byte = getLastValueReceivedOnFace(face);

      if (in_face_state.game_state != in_face_state_[face].game_state) {
        // And the game state changed, so we update ours.
        set_game_state(in_face_state.game_state);
      }
    }

    if (blink_state_.move_bat_face == face && in_face_state_[face].has_bat) {
      // We got confirmation that the bat transferred to the other Blink.
      // Complete the move.
      blink_state_.has_bat = false;
      blink_state_.move_bat_face = FACE_COUNT;

      out_face_state_[face].send_bat = false;
    }

    if (in_face_state.send_bat && !blink_state_.has_bat) {
      // This neighbor Blink is sending us the bat. Accept it.
      blink_state_.has_bat = true;
    }

    // If this face is sending light to us, we propagate it to the opposite
    // face.
    out_face_state_[opposite_face_[face]].send_light = in_face_state.send_light;

    // Cache the parsed neighbor state.
    in_face_state_[face] = in_face_state;
  }
}

void loop() {
  // Update local Blink state based on the state of its neighboors.
  process_neighbors();

  if (buttonLongPressed()) {
    // Long press resets the game to the idle state.
    set_game_state(GAME_STATE_IDLE);
  }

  switch (get_game_state()) {
    case GAME_STATE_IDLE:
      // Run idle loop.
      idle_loop();
      break;
    case GAME_STATE_PLAY:
      // Run play loop.
      play_loop();
      break;
    case GAME_STATE_END_WIN:
    case GAME_STATE_END_LOSE:
      // Run end loop.
      end_loop();
      break;
  }

  // Render this blink visuals and outgoing face values.
  render_blink_state();
}
