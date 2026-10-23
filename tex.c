
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;

#define COLOR(hex) \
_parse_hex_pair(hex, 0), \
_parse_hex_pair(hex, 2), \
_parse_hex_pair(hex, 4)

static const char* version  = "Tex version: v0.1.0\n";
static const char* help     = "Tex:\n\t--help|-h\t\t\tHelp\n\t--version|-v\t\t\tVersion\n\t[filename]\t\t\tOpen file\n";

#define BG 1
#define FG 2
static const char* colors[] = {
  [BG] = "#1d2021",
  [FG] = "#e0def4"
};
static const float opacity   = 0.9f;
static const int   font_size = 16;
static const int   padding_x = 10;
static const int   padding_y = 10;
static const int   tabstop   = 4;

static inline u8 _parse_hex_pair(const char* str, int offset) {
  if (str == NULL)
    return 0;

  if (str[0] == '#')
    offset++;

  char hex[3] = { str[offset], str[offset + 1], '\0' };
  return (u8)strtol(hex, NULL, 16);
}

static inline const char* _parse_cmd_args(int argc, const char** argv) {
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
      fprintf(stderr, "%s", help);
      exit(EXIT_SUCCESS);
    } else if (!strcmp(argv[i], "--version") || !strcmp(argv[i], "-v")) {
      fprintf(stderr, "%s", version);
      exit(EXIT_SUCCESS);
    } else
    return argv[i];
  }

  return NULL;
}

struct str8 {
  u8* data;
  size_t size;
  size_t capacity;
};

struct editor_core_T {
  size_t cur_pos;
  size_t preferred_col;
  bool preferred_col_valid;
  struct str8 buffer;
};

struct state_T {
  int argc;
  const char** argv;
  bool has_file;
  const char* filename;
  bool running;
  size_t row_offset;
  size_t col_offset;
  SDL_Window* window;
  SDL_Renderer* renderer;
  TTF_Font* font;
  struct editor_core_T* core;
};

static inline u8 _parse_hex_pair(const char* str, int offset);
static inline const char* _parse_cmd_args(int argc, const char** argv);

static void string8_init(struct str8* string);
static bool string8_reserve(struct str8* string, size_t needed);
static bool string8_insert(struct str8* string, u8 val, size_t pos);
static bool string8_delete(struct str8* string, size_t pos);
static void string8_dest(struct str8* string);

static void editor_core_init(struct editor_core_T* core);
static void editor_core_dest(struct editor_core_T* core);

static void tex_file_draw(struct state_T* state);
static void tex_window_init(struct state_T* state);
static size_t tex_cursor_row(struct state_T* state);
static size_t tex_visible_rows(struct state_T* state);
static size_t tex_total_rows(const struct str8* buffer);
static int tex_cursor_x(struct state_T* state);
static void tex_scroll_horizontal(struct state_T* state);
static void tex_cursor_draw(struct state_T* state);
static void tex_input_handle(struct state_T* state, const SDL_Event* event);
static void tex_render(struct state_T* state);
static void tex_window_loop(struct state_T* state);
static void tex_window_dest(struct state_T* state);

static void tex_file_draw(struct state_T* state) {
  if (!state->has_file)
    return;

  FILE* fp = fopen(state->filename, "rb");
  if (fp == NULL) {
    fprintf(stderr, "tex: cannot open file '%s': ", state->filename);
    perror(NULL);
    exit(EXIT_FAILURE);
  }

  if (fseek(fp, 0, SEEK_END) != 0) {
    fprintf(stderr, "tex: cannot seek file '%s': ", state->filename);
    perror(NULL);
    fclose(fp);
    exit(EXIT_FAILURE);
  }

  long end = ftell(fp);
  if (end < 0) {
    fprintf(stderr, "tex: cannot determine size of '%s': ", state->filename);
    perror(NULL);
    fclose(fp);
    exit(EXIT_FAILURE);
  }

  if (fseek(fp, 0, SEEK_SET) != 0) {
    fprintf(stderr, "tex: cannot rewind file '%s': ", state->filename);
    perror(NULL);
    fclose(fp);
    exit(EXIT_FAILURE);
  }

  size_t file_size = (size_t)end;
  if (file_size > SIZE_MAX - 1 ||
    !string8_reserve(&state->core->buffer, file_size + 1)) {
    fprintf(stderr, "tex: cannot allocate buffer for '%s'\n", state->filename);
    fclose(fp);
    exit(EXIT_FAILURE);
  }

  struct str8* buffer = &state->core->buffer;
  size_t bytes_read = fread(buffer->data, 1, file_size, fp);
  if (bytes_read != file_size) {
    fprintf(stderr, "tex: error reading file '%s'\n", state->filename);
    fclose(fp);
    exit(EXIT_FAILURE);
  }

  if (fclose(fp) != 0) {
    fprintf(stderr, "tex: error closing file '%s': ", state->filename);
    perror(NULL);
    exit(EXIT_FAILURE);
  }

  buffer->size = bytes_read;
  buffer->data[buffer->size] = '\0';
  state->core->cur_pos = 0;
}

static void string8_init(struct str8* string) {
  string->data = calloc(1, sizeof(*string->data));

  if (string->data == NULL) {
    fprintf(stderr, "tex: cannot allocate string buffer\n");
    exit(EXIT_FAILURE);
  }

  string->size = 0;
  string->capacity = 1;
}

static bool string8_reserve(struct str8* string, size_t needed) {
  if (needed <= string->capacity)
    return true;

  size_t capacity = string->capacity;

  while (capacity < needed) {
    if (capacity > SIZE_MAX / 2) {
      capacity = needed;
      break;
    }

    capacity *= 2;
  }

  if (capacity > SIZE_MAX / sizeof(*string->data))
    return false;

  u8* data = realloc(string->data, capacity * sizeof(*data));

  if (data == NULL)
    return false;

  string->data = data;
  string->capacity = capacity;
  return true;
}

static bool string8_insert(struct str8* string, u8 val, size_t pos) {
  if (string->data == NULL || pos > string->size)
    return false;

  if (string->size > SIZE_MAX - 2 ||
    !string8_reserve(string, string->size + 2))
    return false;

  memmove(
    string->data + pos + 1,
    string->data + pos,
    string->size - pos + 1
  );

  string->data[pos] = val;
  string->size++;

  return true;
}

static bool string8_delete(struct str8* string, size_t pos) {
  if (string->data == NULL || pos >= string->size)
    return false;

  memmove(
    string->data + pos,
    string->data + pos + 1,
    string->size - pos
  );

  string->size--;
  return true;
}

static void string8_dest(struct str8* string) {
  free(string->data);
  *string = (struct str8){0};
}

static void editor_core_init(struct editor_core_T* core) {
  core->cur_pos = 0;
  core->preferred_col = 0;
  core->preferred_col_valid = false;

  string8_init(&core->buffer);
}

static void editor_core_dest(struct editor_core_T* core) {
  string8_dest(&core->buffer);

  core->cur_pos = 0;
  core->preferred_col = 0;
  core->preferred_col_valid = false;
}

static size_t tex_line_start(const struct str8* buffer, size_t pos) {
  if (pos > buffer->size)
    pos = buffer->size;

  while (pos > 0 && buffer->data[pos - 1] != '\n')
    pos--;

  return pos;
}

static size_t tex_line_end(const struct str8* buffer, size_t pos) {
  if (pos > buffer->size)
    pos = buffer->size;

  while (pos < buffer->size && buffer->data[pos] != '\n')
    pos++;

  return pos;
}

static void tex_move_vertical(struct editor_core_T* core, int direction) {
  struct str8* buffer = &core->buffer;

  size_t start = tex_line_start(buffer, core->cur_pos);
  size_t column = core->cur_pos - start;

  if (!core->preferred_col_valid) {
    core->preferred_col = column;
    core->preferred_col_valid = true;
  }

  size_t target_start;
  size_t target_end;

  if (direction < 0) {
    if (start == 0)
      return;

    target_end = start - 1;
    target_start = tex_line_start(buffer, target_end);
  } else {
    size_t current_end = tex_line_end(buffer, core->cur_pos);

    if (current_end >= buffer->size)
      return;

    target_start = current_end + 1;
    target_end = tex_line_end(buffer, target_start);
  }

  size_t target_length = target_end - target_start;
  size_t target_column = core->preferred_col;

  core->cur_pos = target_start +
    (target_column < target_length ? target_column : target_length);
}
static size_t tex_cursor_row(struct state_T* state) {
  size_t row = 0;

  for (size_t i = 0; i < state->core->cur_pos; i++) {
    if (state->core->buffer.data[i] == '\n')
      row++;
  }

  return row;
}

static size_t tex_visible_rows(struct state_T* state) {
  int width, height;
  SDL_GetRendererOutputSize(state->renderer, &width, &height);

  int line_height = TTF_FontLineSkip(state->font);
  int available = height - 2 * padding_y;

  if (line_height <= 0 || available <= 0)
    return 1;

  size_t rows = (size_t)(available / line_height);
  return rows > 0 ? rows : 1;
}

static size_t tex_total_rows(const struct str8* buffer) {
  size_t rows = 1;

  for (size_t i = 0; i < buffer->size; i++) {
    if (buffer->data[i] == '\n')
      rows++;
  }

  return rows;
}

static int tex_cursor_x(struct state_T* state) {
  struct editor_core_T* core = state->core;
  struct str8* buffer = &core->buffer;

  size_t start = tex_line_start(buffer, core->cur_pos);
  size_t len = core->cur_pos - start;

  if (len == 0)
    return 0;

  char* prefix = malloc(len + 1);

  if (prefix == NULL) {
    fprintf(stderr, "tex: cannot allocate cursor prefix\n");
    exit(EXIT_FAILURE);
  }

  memcpy(prefix, buffer->data + start, len);
  prefix[len] = '\0';

  int width = 0;

  if (TTF_SizeUTF8(state->font, prefix, &width, NULL) < 0)
    width = 0;

  free(prefix);
  return width;
}

static void tex_scroll_horizontal(struct state_T* state) {
  int width, height;
  SDL_GetRendererOutputSize(state->renderer, &width, &height);

  int available_width = width - 2 * padding_x;
  if (available_width <= 0)
    return;

  int cursor_x = tex_cursor_x(state);

  if ((size_t)cursor_x < state->col_offset) {
    state->col_offset = cursor_x;
  } else if ((size_t)cursor_x >= state->col_offset + available_width) {
    state->col_offset = cursor_x - available_width + 1;
  }
}

static void tex_scroll(struct state_T* state) {
  size_t cursor_row = tex_cursor_row(state);
  size_t visible_rows = tex_visible_rows(state);

  if (cursor_row < state->row_offset) {
    state->row_offset = cursor_row;
  } else if (cursor_row >= state->row_offset + visible_rows) {
    state->row_offset = cursor_row - visible_rows + 1;
  }
}
static void tex_cursor_draw(struct state_T* state) {
  int cursor_x = padding_x + tex_cursor_x(state) - state->col_offset;
  int cursor_h = TTF_FontHeight(state->font);
  size_t cursor_row = tex_cursor_row(state);

  size_t visible_rows = tex_visible_rows(state);

  if (cursor_row < state->row_offset ||
      cursor_row - state->row_offset >= visible_rows)
    return;

  int cursor_y = padding_y + (int)(cursor_row - state->row_offset) *
    TTF_FontLineSkip(state->font);
  int width, height;
  SDL_GetRendererOutputSize(state->renderer, &width, &height);

  if (cursor_x < padding_x || cursor_x >= width - padding_x)
    return;
  SDL_SetRenderDrawColor(state->renderer, 196, 167, 231, 255);

  SDL_RenderDrawLine(
    state->renderer,
    cursor_x,
    cursor_y,
    cursor_x,
    cursor_y + cursor_h
  );
}

static void tex_window_init(struct state_T* state) {
  state->filename = _parse_cmd_args(state->argc, state->argv);
  state->has_file = state->filename ? true : false;
  tex_file_draw(state);

  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    fprintf(stderr, "tex: cannot initialize SDL: %s\n", SDL_GetError());
    exit(EXIT_FAILURE);
  }

  state->window = SDL_CreateWindow(
    "Tex",
    SDL_WINDOWPOS_UNDEFINED,
    SDL_WINDOWPOS_UNDEFINED,
    640,
    480,
    SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
  );

  if (state->window == NULL) {
    fprintf(stderr, "tex: cannot create window: %s\n", SDL_GetError());
    SDL_Quit();
    exit(EXIT_FAILURE);
  }

  if (SDL_SetWindowOpacity(state->window, opacity) < 0)
    fprintf(stderr, "tex: cannot set window opacity: %s\n", SDL_GetError());

  if (TTF_Init() < 0) {
    fprintf(stderr, "tex: cannot initialize SDL_ttf: %s\n", TTF_GetError());
    SDL_DestroyWindow(state->window);
    SDL_Quit();
    exit(EXIT_FAILURE);
  }

  state->font = TTF_OpenFont(
    "/usr/share/fonts/TTF/CaskaydiaMonoNerdFont-Bold.ttf",
    font_size
  );

  if (state->font == NULL) {
    fprintf(stderr, "tex: cannot load font: %s\n", TTF_GetError());
    TTF_Quit();
    SDL_DestroyWindow(state->window);
    SDL_Quit();
    exit(EXIT_FAILURE);
  }

  state->renderer = SDL_CreateRenderer(
    state->window,
    -1,
    SDL_RENDERER_ACCELERATED
  );

  if (state->renderer == NULL) {
    fprintf(stderr, "tex: cannot create renderer: %s\n", SDL_GetError());
    TTF_CloseFont(state->font);
    TTF_Quit();
    SDL_DestroyWindow(state->window);
    SDL_Quit();
    exit(EXIT_FAILURE);
  }

  state->running = true;
}

static void tex_input_handle(struct state_T* state, const SDL_Event* event) {
  struct editor_core_T* core = state->core;
  struct str8* buffer = &core->buffer;

  if (event->type == SDL_TEXTINPUT) {
    const char* text = event->text.text;

    for (size_t i = 0; text[i] != '\0'; i++) {
      if (!string8_insert(buffer, (u8)text[i], core->cur_pos)) {
        fprintf(stderr, "tex: failed to insert text\n");
        break;
      }

      core->cur_pos++;
    }

    core->preferred_col_valid = false;
    tex_scroll(state);
    tex_scroll_horizontal(state);
    return;
  }

  if (event->type != SDL_KEYDOWN)
    return;

  switch (event->key.keysym.sym) {
    case SDLK_BACKSPACE: {
      if (core->cur_pos > 0 &&
        string8_delete(buffer, core->cur_pos - 1))
        core->cur_pos--;

      core->preferred_col_valid = false;
    } break;

    case SDLK_DELETE: {
      string8_delete(buffer, core->cur_pos);
      core->preferred_col_valid = false;
    } break;

    case SDLK_LEFT: {
      if (core->cur_pos > 0)
        core->cur_pos--;

      core->preferred_col_valid = false;
    } break;

    case SDLK_RIGHT: {
      if (core->cur_pos < buffer->size)
        core->cur_pos++;

      core->preferred_col_valid = false;
    } break;

    case SDLK_UP: {
      tex_move_vertical(core, -1);
    } break;

    case SDLK_DOWN: {
      tex_move_vertical(core, 1);
    } break;

    case SDLK_HOME: {
      core->cur_pos = tex_line_start(buffer, core->cur_pos);
      core->preferred_col_valid = false;
    } break;

    case SDLK_END: {
      core->cur_pos = tex_line_end(buffer, core->cur_pos);
      core->preferred_col_valid = false;
    } break;

    case SDLK_RETURN:
    case SDLK_KP_ENTER: {
      if (string8_insert(buffer, '\n', core->cur_pos))
        core->cur_pos++;

      core->preferred_col_valid = false;
    } break;

    case SDLK_TAB: {
      for (int i = 0; i < tabstop; i++) {
        if (!string8_insert(buffer, ' ', core->cur_pos)) {
          fprintf(stderr, "tex: failed to insert tab\n");
          break;
        }
        core->cur_pos++;
      }
      core->preferred_col_valid = false;
    } break;

    default:
      break;
  }
  core->preferred_col_valid = false;
  tex_scroll(state);
  tex_scroll_horizontal(state);
}

static void tex_render(struct state_T* state) {
  struct str8* buffer = &state->core->buffer;

  SDL_Color fg = { COLOR(colors[FG]), 255 };

  int line_y = padding_y;
  int line_height = TTF_FontLineSkip(state->font);
  size_t start = 0;
  size_t row = 0;
  size_t visible_rows = tex_visible_rows(state);

  SDL_SetRenderDrawColor(
    state->renderer, COLOR(colors[BG]), 255
  );
  SDL_RenderClear(state->renderer);

  while (start < buffer->size && row < state->row_offset) {
    if (buffer->data[start++] == '\n')
      row++;
  }

  size_t end = start;

  while (row < state->row_offset + visible_rows &&
    end <= buffer->size) {
    if (end < buffer->size && buffer->data[end] != '\n') {
      end++;
      continue;
    }

    size_t len = end - start;

    if (len > 0) {
      char* line = malloc(len + 1);

      if (line == NULL) {
        fprintf(stderr, "tex: cannot allocate line buffer\n");
        exit(EXIT_FAILURE);
      }

      memcpy(line, buffer->data + start, len);
      line[len] = '\0';

      SDL_Surface* surface =
        TTF_RenderUTF8_Blended(state->font, line, fg);

      free(line);

      if (surface != NULL) {
        SDL_Texture* texture =
          SDL_CreateTextureFromSurface(state->renderer, surface);

        if (texture != NULL) {
          SDL_Rect dest = {
            .x = padding_x - state->col_offset,
            .y = line_y,
            .w = surface->w,
            .h = surface->h
          };

          SDL_RenderCopy(state->renderer, texture, NULL, &dest);
          SDL_DestroyTexture(texture);
        }

        SDL_FreeSurface(surface);
      }
    }

    line_y += line_height;
    row++;

    if (end == buffer->size)
      break;

    start = end + 1;
    end = start;
  }

  tex_cursor_draw(state);
  SDL_RenderPresent(state->renderer);
}

static void tex_window_loop(struct state_T* state) {
  SDL_Event event;
  SDL_StartTextInput();

  while (state->running) {
    if (SDL_WaitEventTimeout(&event, 16)) {
      do {
        switch (event.type) {
          case SDL_QUIT: {
            state->running = false;
          } break;

          case SDL_KEYDOWN:
          case SDL_TEXTINPUT: {
            tex_input_handle(state, &event);
          } break;

          case SDL_MOUSEWHEEL: {
            int direction = event.wheel.y;

            if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
              direction = -direction;

            size_t visible_rows = tex_visible_rows(state);
            size_t total_rows = tex_total_rows(&state->core->buffer);
            size_t max_offset = total_rows > visible_rows
              ? total_rows - visible_rows
              : 0;

            if (state->row_offset > max_offset)
              state->row_offset = max_offset;

            size_t amount = (size_t)(direction < 0 ? -direction : direction) * 3;

            if (direction > 0) {
              state->row_offset = amount > state->row_offset
                ? 0
                : state->row_offset - amount;
            } else if (direction < 0) {
              state->row_offset = amount > max_offset - state->row_offset
                ? max_offset
                : state->row_offset + amount;
            }
          } break;

          default:
            break;
        }
      } while (SDL_PollEvent(&event));
    }

    tex_render(state);
  }

  SDL_StopTextInput();
}

static void tex_window_dest(struct state_T* state) {
  if (state->renderer != NULL)
    SDL_DestroyRenderer(state->renderer);

  if (state->window != NULL)
    SDL_DestroyWindow(state->window);

  if (state->font != NULL)
    TTF_CloseFont(state->font);

  TTF_Quit();
  SDL_Quit();
}

int main(int argc, const char** argv) {
  struct editor_core_T core;
  editor_core_init(&core);

  struct state_T state = {
    .argc = argc,
    .argv = argv,
    .running = false,
    .row_offset = 0,
    .col_offset = 0,
    .window = NULL,
    .renderer = NULL,
    .font = NULL,
    .core = &core,
  };

  tex_window_init(&state);
  tex_window_loop(&state);
  tex_window_dest(&state);
  editor_core_dest(&core);

  return EXIT_SUCCESS;
}

