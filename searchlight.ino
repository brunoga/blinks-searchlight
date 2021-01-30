#include <blinklib.h>

#define GAME_STATE_IDLE 0
#define GAME_STATE_PLAY 1
#define GAME_STATE_END_WIN 2
#define GAME_STATE_END_LOSE 3

#define PLAY_TIME_IN_SECONDS 10

typedef byte GameState;
static GameState game_state_;

union FaceState {
  struct {
    byte game_state : 2;
    bool send_light : 1;
    bool is_lit : 1;
    bool send_marker : 1;
    bool has_marker : 1;
    byte reserved : 2;
  };

  byte as_byte;
};
static FaceState in_face_state_[FACE_COUNT];
static FaceState out_face_state_[FACE_COUNT];

struct BlinkState {
  bool is_player;
  bool has_marker;
  bool time_tracker;
  byte move_marker_face;
};
static BlinkState blink_state_;

static const byte opposite_face_[] = {3, 4, 5, 0, 1, 2};

static Timer game_play_timer_;

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

  blink_state_.move_marker_face = FACE_COUNT;
}

static void move_marker() {
  // Keep track of the number of possible moves and the faces the marker can
  // move through.
  byte possible_move_count = 0;
  int8_t possible_move_face[FACE_COUNT];

  FOREACH_FACE(face) {
    if (!isValueReceivedOnFaceExpired(face) && !in_face_state_[face].is_lit) {
      // Face is not expired and is not lit. Marker can move through there.
      possible_move_face[possible_move_count] = face;
      possible_move_count++;
    }
  }

  if (possible_move_count == 0) {
    // The marker can not be moved anywhere. Game over.
    set_game_state(GAME_STATE_END_WIN);

    return;
  }

  // Pick a random face from the ones we can move to.
  byte move_marker_face = possible_move_face[random(possible_move_count - 1)];

  // Send the marker through there.
  out_face_state_[move_marker_face].send_marker = true;

  // And remember the face.
  blink_state_.move_marker_face = move_marker_face;
}

static void render_blink_state() {
  switch (get_game_state()) {
    case GAME_STATE_IDLE:
      // When idle, just render the Blink as yellow.
      //
      // TODO(bga): It might be nice to animate the "flashlight" face as if it
      // was searching for something.
      setColor(YELLOW);
      break;
    case GAME_STATE_PLAY:
      // Play state. Things are a bit more complex now.
      if (is_lit()) {
        // This Blink is lit.
        if (blink_state_.is_player) {
          // It is a player Blink. Reset all face to off.
          setColor(OFF);

          // And set face 0 to white.
          setColorOnFace(WHITE, 0);
        } else if (blink_state_.has_marker) {
          // It has the marker and was caught by a flashlight. Render it as red.
          setColor(RED);
        } else {
          // Just set the color to white to show we are lit.
          setColor(WHITE);
        }
      } else if (blink_state_.has_marker) {
        // This Blink is not lit and has the marker. Render as off.
        setColor(OFF);
      } else {
        // Not lit and no marker. Render as off.
        setColor(OFF);
      }
      break;
    case GAME_STATE_END_WIN:
      // We won!
      if (blink_state_.has_marker) {
        // If we have the marker, render as red so all players can see where it
        // was.
        setColor(RED);
      } else {
        // No marker. Render as blue, which is ourt victory color.
        setColor(BLUE);
      }
      break;
    case GAME_STATE_END_LOSE:
      // We lost. Render as red.
      //
      // TODO(bga): Maybe show where the actual marker was?
      setColor(RED);
      break;
  }

  FOREACH_FACE(face) {
    out_face_state_[face].game_state = get_game_state();
    out_face_state_[face].has_marker = blink_state_.has_marker;
    out_face_state_[face].is_lit = is_lit();

    setValueSentOnFace(out_face_state_[face].as_byte, face);
  }
}

static void idle_loop() {
  // Reset our state.
  blink_state_.has_marker = false;
  blink_state_.is_player = false;
  blink_state_.time_tracker = false;
  blink_state_.move_marker_face = FACE_COUNT;

  if (buttonSingleClicked() && !hasWoken()) {
    // The button was single-clicked and it was not a wake-up click.
    if (isAlone()) {
      // We are alone, so we are a player Blink now.
      blink_state_.is_player = true;
    } else {
      // We are not alone, so we are a piece of the board that has the marker.
      blink_state_.has_marker = true;

      // And we will also keep track of the timer.
      game_play_timer_.set(PLAY_TIME_IN_SECONDS * 1000);
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
  // if (!move_timer_.isExpired()) return;

  if (blink_state_.has_marker && blink_state_.move_marker_face == FACE_COUNT) {
    // We hold the marker and we are not transferring it yet. Check if whatever
    // Blink that send it to us still thinks it holds it.
    FOREACH_FACE(face) {
      if (in_face_state_[face].has_marker) {
        // It does. Do not try to move it to another Blink yet.
        return;
      }
    }

    // Move marker to another Blink.
    move_marker();
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

    if (blink_state_.move_marker_face == face &&
        in_face_state_[face].has_marker) {
      // We got confirmation that the marker transferred to the other Blink.
      // Complete the move.
      blink_state_.has_marker = false;
      blink_state_.move_marker_face = FACE_COUNT;

      out_face_state_[face].send_marker = false;
    }

    if (in_face_state.send_marker && !blink_state_.has_marker) {
      // This neighbor Blink is sending us the marker. Accept it.
      blink_state_.has_marker = true;
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
