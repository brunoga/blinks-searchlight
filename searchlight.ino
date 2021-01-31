#include <blinklib.h>

#define GAME_STATE_IDLE 0
#define GAME_STATE_PLAY 1
#define GAME_STATE_END_WIN 2
#define GAME_STATE_END_LOSE 3

#define PLAY_TIME_IN_SECONDS 60
#define FADE_TO_BLACK_TIME_IN_MS 500

#define LIGHT_COLOR WHITE
#define BAT_COLOR GREEN
#define WIN_COLOR BLUE
#define LOSE_COLOR RED

// Uncomment this to enable debug features:
//#define DEBUG_ALWAYS_SHOW_BAT  // Always see the bat
//#define DEBUG_DISABLE_BAT      // To test searchlight logic

typedef byte GameState;
static GameState game_state_;

union FaceState {
  struct {
    byte game_state : 2;
    bool send_light : 1;
    bool is_lit : 1;
    bool send_bat : 1;
    bool has_bat : 1;
    byte reserved : 2;  // Reserved by Blinklib.
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
  byte src_bat_face;
  byte dst_bat_face;
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

  blink_state_.src_bat_face = FACE_COUNT;
  blink_state_.dst_bat_face = FACE_COUNT;
}

static byte get_valid_dst_bat_face() {
  if (blink_state_.src_bat_face == FACE_COUNT) {
    // No src face so we just started. Pretend the bat entered from face 0.
    blink_state_.src_bat_face = 0;
  }

  // Get the opposite face to the one the bat came from.
  byte opposite_face = opposite_face_[blink_state_.src_bat_face];

  // Setup forward faces.
  byte forward_faces[3] = {
      (byte)(((opposite_face - 1) + FACE_COUNT) % FACE_COUNT),
      opposite_face,
      (byte)(((opposite_face + 1) + FACE_COUNT) % FACE_COUNT),
  };

  // And all other faces (except for the opposite one).
  byte other_faces[2] = {
      (byte)(((blink_state_.src_bat_face - 1) + FACE_COUNT) % FACE_COUNT),
      (byte)(((blink_state_.src_bat_face + 1) + FACE_COUNT) % FACE_COUNT),
  };

  // Keep track of valid destination faces.
  byte face_buffer[3];
  byte face_buffer_count = 0;

  // First try forward faces.
  for (byte i = 0; i < 3; ++i) {
    if (!isValueReceivedOnFaceExpired(forward_faces[i])) {
      if (forward_faces[i] != opposite_face &&
          in_face_state_[forward_faces[i]].is_lit) {
        continue;
      }

      face_buffer[face_buffer_count] = forward_faces[i];
      face_buffer_count++;
    }
  }

  if (face_buffer_count == 0) {
    // Now try any other faces that are not the one we just arrived from.
    for (byte i = 0; i < 2; ++i) {
      if (isValueReceivedOnFaceExpired(other_faces[i]) ||
          in_face_state_[other_faces[i]].is_lit) {
        continue;
      }

      face_buffer[face_buffer_count] = other_faces[i];
      face_buffer_count++;
    }

    if (face_buffer_count == 0) {
      // Finally try the source face.
      if (isValueReceivedOnFaceExpired(blink_state_.src_bat_face) ||
          in_face_state_[blink_state_.src_bat_face].is_lit) {
        return FACE_COUNT;
      }

      face_buffer[face_buffer_count] = blink_state_.src_bat_face;
      face_buffer_count++;
    }
  }

  return face_buffer[random(face_buffer_count - 1)];
}

static void move_bat() {
  // Try to find a valid sestination face.
  blink_state_.dst_bat_face = get_valid_dst_bat_face();

  if (blink_state_.dst_bat_face == FACE_COUNT) {
    // The bat can not be moved anywhere. Game over.
    set_game_state(GAME_STATE_END_WIN);

    return;
  }
}

static byte idle_searchlight_face_ = 0;
static Timer idle_searchlight_timer_;

static void render_idle_animation() {
  setColorOnFace(WHITE, idle_searchlight_face_);

  if (!idle_searchlight_timer_.isExpired()) {
    return;
  }

  idle_searchlight_timer_.set(200);

  int8_t move = random(2) - 1;

  idle_searchlight_face_ =
      (((idle_searchlight_face_ + move) + FACE_COUNT) % FACE_COUNT);
}

static bool bat_wing_position_;
static Timer bat_wing_position_timer_;

static void render_bat_animation(Color color) {
  // Render bat wings based on the direction the bat came from and the current
  // wing position.
  setColorOnFace(
      color, (blink_state_.src_bat_face + 1 + bat_wing_position_) % FACE_COUNT);
  setColorOnFace(color, (blink_state_.src_bat_face + (FACE_COUNT - 1) -
                         bat_wing_position_) %
                            FACE_COUNT);

  if (!bat_wing_position_timer_.isExpired()) {
    return;
  }

  bat_wing_position_timer_.set(200);

  // Change wing position.
  bat_wing_position_ = !bat_wing_position_;
}

static void render_blink_state() {
  // Reset colors before starting to render.
  setColor(OFF);

  switch (get_game_state()) {
    case GAME_STATE_IDLE:
      // Render idle animation.
      render_idle_animation();
      break;
    case GAME_STATE_PLAY:
      // Play state. Things are a bit more complex now.
      if (is_lit()) {
        // This Blink is lit.

        if (blink_state_.is_player) {
          // It is a player Blink. Set face 0 to white.
          setColorOnFace(WHITE, 0);

          break;
        }

        // Default state is light color.
        setColor(LIGHT_COLOR);

        if (blink_state_.has_bat) {
          // It has the bat and it was caught by a flashlight. Render it as
          // the bat.
          render_bat_animation(BAT_COLOR);
        }
      } else if (blink_state_.has_bat) {
#ifdef DEBUG_ALWAYS_SHOW_BAT
        render_bat_animation(BAT_COLOR);
#else
        // This Blink is not lit and has the bat. Render it according to the
        // fade to black timer (i.e. it will start at the given color and fade
        // to off).
        Color bat_color;
        if (blink_state_.src_bat_face != FACE_COUNT &&
            in_face_state_[blink_state_.src_bat_face].is_lit) {
          // Bat came form a lit face. Git a hint that it came here.
          bat_color = dim(BAT_COLOR, 50);
        } else {
          bat_color = dim(BAT_COLOR, (MAX_BRIGHTNESS *
                                      fade_to_black_timer_.getRemaining()) /
                                         FADE_TO_BLACK_TIME_IN_MS);
        }

        render_bat_animation(bat_color);
#endif
      } else {
        // Nothing special here. Pitch black.
        setColor(OFF);
      }
      break;
    case GAME_STATE_END_WIN:
      // We won!
      if (blink_state_.has_bat) {
        if (blink_state_.is_player) {
          // Render flashlight to indicate the player has the bat.
          setColorOnFace(WHITE, 0);
        }

        // If we have the bat, render it so all players can see where it
        // was.
        render_bat_animation(BAT_COLOR);
      } else {
        // No bat. Render as the win color.
        setColor(WIN_COLOR);
      }
      break;
    case GAME_STATE_END_LOSE:
      // We lost.
      setColor(LOSE_COLOR);

      if (blink_state_.has_bat) {
        // If we have the bat, render it so all players can see where it
        // was.
        render_bat_animation(BAT_COLOR);
      }
      break;
  }

  // Update reporting state for all faces.
  FOREACH_FACE(face) {
    out_face_state_[face].game_state = get_game_state();
    out_face_state_[face].has_bat = blink_state_.has_bat;
    out_face_state_[face].is_lit = is_lit();
    if (face == blink_state_.dst_bat_face) {
      out_face_state_[face].send_bat = true;
    } else {
      out_face_state_[face].send_bat = false;
    }

    setValueSentOnFace(out_face_state_[face].as_byte, face);
  }
}

static void idle_loop() {
  // Reset our state.
  blink_state_.has_bat = false;
  blink_state_.is_player = false;
  blink_state_.time_tracker = false;
  blink_state_.game_started = true;
  blink_state_.src_bat_face = FACE_COUNT;
  blink_state_.dst_bat_face = FACE_COUNT;

  if (buttonSingleClicked() && !hasWoken()) {
    // The button was single-clicked and it was not a wake-up click.
    if (isAlone()) {
      // We are alone, so we are a player Blink now.
      blink_state_.is_player = true;
    } else {
#ifndef DEBUG_DISABLE_BAT
      // We are not alone, so we are a piece of the board that has the bat.
      blink_state_.has_bat = true;
#endif

      // And we will also keep track of the timer.
      game_play_timer_.set(1000L * PLAY_TIME_IN_SECONDS);
      blink_state_.time_tracker = true;
    }

    // Switch to the play state.
    set_game_state(GAME_STATE_PLAY);
  }
}

static void play_player_loop() {
  if (blink_state_.has_bat) {
    // Bat ran head-on into us, so we were able to catch it.
    set_game_state(GAME_STATE_END_WIN);

    return;
  }

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

  if (blink_state_.has_bat && blink_state_.dst_bat_face == FACE_COUNT) {
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

    if (blink_state_.dst_bat_face == face && in_face_state_[face].has_bat) {
      // We got confirmation that the bat transferred to the other Blink.
      // Complete the move.
      blink_state_.has_bat = false;

      blink_state_.src_bat_face = FACE_COUNT;
      blink_state_.dst_bat_face = FACE_COUNT;

      out_face_state_[face].send_bat = false;
    }

    if (in_face_state.send_bat && !blink_state_.has_bat) {
      // This neighbor Blink is sending us the bat. Accept it.
      blink_state_.has_bat = true;
      blink_state_.src_bat_face = face;
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
