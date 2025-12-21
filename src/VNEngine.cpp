// VNEngine.cpp
#include "VNEngine/VNEngine.hpp"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <iostream>
#include <sol/sol.hpp>
#include <unordered_map>

class VNEngine::Impl {
public:
  SDL_Window *m_window = nullptr;
  SDL_Renderer *m_renderer = nullptr;
  int m_screenWidth = 0;
  int m_screenHeight = 0;

  TTF_Font *m_font = nullptr;
  SDL_Texture *m_background = nullptr;
  SDL_Texture *m_textBox = nullptr;
  SDL_Texture *m_nameBox = nullptr;
  std::unordered_map<std::string, SDL_Texture *> m_characters;

  sol::state m_lua;
  bool m_scriptFinished = false;

  std::string m_currentSpeaker;
  std::string m_currentName;
  std::string m_currentText;
  float m_typewriterTimer = 0.0f;
  int m_displayedChars = 0;
  static constexpr float CHARS_PER_SECOND = 60.0f;

  bool Init(const char *title, int w, int h, bool fullscreen) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
      std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
      return false;
    }

    if (TTF_Init() == false) {
      std::cerr << "TTF_Init failed: " << SDL_GetError() << '\n';
      return false;
    }

    Uint32 flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (fullscreen)
      flags |= SDL_WINDOW_FULLSCREEN;

    m_window = SDL_CreateWindow(title, w, h, flags);
    if (!m_window) {
      std::cerr << "CreateWindow failed: " << SDL_GetError() << '\n';
      return false;
    }

    m_renderer = CreateBestRenderer(m_window);
    if (!m_renderer) {
      std::cerr << "All renderer attempts failed\n";
      return false;
    }

    m_screenWidth = w;
    m_screenHeight = h;

    m_font = TTF_OpenFont("assets/fonts/Montserrat-Medium.ttf", 36);
    if (!m_font) {
      std::cerr << "VNEngine: Failed to load font\n";
    }

    m_textBox = CreateSolidTexture(m_screenWidth,
                                   static_cast<int>(m_screenHeight * 0.25f), 20,
                                   20, 40, 220);
    m_nameBox = CreateSolidTexture(300, 60, 40, 40, 80, 240);

    m_lua.open_libraries(sol::lib::base, sol::lib::string);

    m_lua.set_function("bg",
                       [this](const std::string &f) { ShowBackground(f); });
    m_lua.set_function(
        "show", [this](const std::string &c, sol::optional<std::string> pos) {
          ShowCharacter(c, pos.value_or("center"));
        });
    m_lua.set_function("hide",
                       [this](const std::string &c) { HideCharacter(c); });
    m_lua.set_function("say", [this](sol::variadic_args va) {
      std::string speaker = va[0].as<std::string>();
      std::string name = va.size() >= 2 ? va[1].as<std::string>() : speaker;
      std::string text = va[2].as<std::string>();
      Say(speaker, name, text);
    });
    m_lua.set_function("narrate", [this](const std::string &t) { Narrate(t); });

    return true;
  }

  void ShutDown() {
    if (m_background) {
      SDL_DestroyTexture(m_background);
      m_background = nullptr;
    }
    if (m_textBox) {
      SDL_DestroyTexture(m_textBox);
      m_textBox = nullptr;
    }
    if (m_nameBox) {
      SDL_DestroyTexture(m_nameBox);
      m_nameBox = nullptr;
    }

    for (auto &[id, tex] : m_characters) {
      SDL_DestroyTexture(tex);
    }
    m_characters.clear();

    if (m_font) {
      TTF_CloseFont(m_font);
      m_font = nullptr;
    }
    if (m_renderer) {
      SDL_DestroyRenderer(m_renderer);
      m_renderer = nullptr;
    }
    if (m_window) {
      SDL_DestroyWindow(m_window);
      m_window = nullptr;
    }

    TTF_Quit();
    SDL_Quit();
  }

  bool PollEvent(VNEvent &e) {
    SDL_Event sdl;
    if (!SDL_PollEvent(&sdl))
      return false;

    switch (sdl.type) {
    case SDL_EVENT_QUIT:
      e = {VNEvent::Quit, 0};
      return true;
    case SDL_EVENT_KEY_DOWN:
      e = {VNEvent::KeyDown, sdl.key.key};
      return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
      e = {VNEvent::MouseDown, 0};
      return true;
    default:
      e = {VNEvent::Unknown, 0};
      return true;
    }
  }

  void Present() { SDL_RenderPresent(m_renderer); }

  void HandleEvent(const VNEvent &e) {
    if (!m_renderer)
      return;

    if (e.type == VNEvent::MouseDown || e.type == VNEvent::KeyDown) {
      // Advance typewriter or clear text
      if (m_displayedChars >= static_cast<int>(m_currentText.length())) {
        m_currentText.clear();
        m_displayedChars = 0;
        m_typewriterTimer = 0.0f;
      } else {
        m_displayedChars = static_cast<int>(m_currentText.length());
      }
    }
  }

  void Update(float dt) {
    if (m_currentText.empty())
      return;

    if (m_displayedChars < static_cast<int>(m_currentText.length())) {
      m_typewriterTimer += dt;
      int charsToAdd = static_cast<int>(m_typewriterTimer * CHARS_PER_SECOND);
      if (charsToAdd > 0) {
        m_displayedChars += charsToAdd;
        m_typewriterTimer -= charsToAdd / CHARS_PER_SECOND;
        if (m_displayedChars > static_cast<int>(m_currentText.length()))
          m_displayedChars = m_currentText.length();
      }
    }
  }

  void Draw() {
    SDL_RenderClear(m_renderer);

    if (m_background) {
      SDL_RenderTexture(m_renderer, m_background, nullptr, nullptr);
    }

    // Characters (simple layout)
    float xPos = m_screenWidth * 0.1f;
    const float spacing = m_screenWidth * 0.3f;
    for (const auto &[id, tex] : m_characters) {
      float tw = 0, th = 0;
      SDL_GetTextureSize(tex, &tw, &th);
      float scale = (m_screenHeight * 0.8f) / th;
      SDL_FRect dst = {xPos,
                       m_screenHeight - th * scale - m_screenHeight * 0.1f,
                       tw * scale, th * scale};
      SDL_RenderTexture(m_renderer, tex, nullptr, &dst);
      xPos += spacing;
    }

    if (!m_currentText.empty()) {
      // Textbox
      if (m_textBox) {
        SDL_FRect tb{0, m_screenHeight * 0.75f,
                     static_cast<float>(m_screenWidth), m_screenHeight * 0.25f};
        SDL_RenderTexture(m_renderer, m_textBox, nullptr, &tb);
      }

      // Namebox
      if (!m_currentSpeaker.empty() && m_nameBox) {
        SDL_FRect nb{50, m_screenHeight * 0.75f - 70, 300, 60};
        SDL_RenderTexture(m_renderer, m_nameBox, nullptr, &nb);

        SDL_Texture *nameTex = RenderText(m_currentName, {255, 255, 255, 255});
        if (nameTex) {
          float w, h;
          SDL_GetTextureSize(nameTex, &w, &h);
          SDL_FRect dst{70, m_screenHeight * 0.75f - 60, w, h};
          SDL_RenderTexture(m_renderer, nameTex, nullptr, &dst);
          SDL_DestroyTexture(nameTex);
        }
      }

      // Dialogue text
      std::string visible = m_currentText.substr(0, m_displayedChars);
      SDL_Texture *textTex = RenderText(visible, {255, 255, 255, 255});
      if (textTex) {
        float w, h;
        SDL_GetTextureSize(textTex, &w, &h);
        float margin = 70.0f;
        float maxW = m_screenWidth - 2 * margin;
        float scale = (w > maxW) ? maxW / w : 1.0f;
        SDL_FRect dst{margin, m_screenHeight * 0.80f, w * scale, h * scale};
        SDL_RenderTexture(m_renderer, textTex, nullptr, &dst);
        SDL_DestroyTexture(textTex);
      }
    }
  }

  // Private helpers
  SDL_Renderer *CreateBestRenderer(SDL_Window *window) {
    const char *preferred[] = {"gpu", "vulkan", "opengl", "opengles2"};
    for (const char *name : preferred) {
      SDL_Renderer *r = SDL_CreateRenderer(window, name);
      if (r) {
        SDL_Log("Renderer created: %s", name);
        SDL_SetRenderVSync(r, 1);
        return r;
      }
      std::cout << "Failed " << name << ": " << SDL_GetError() << '\n';
    }
    SDL_Renderer *r = SDL_CreateRenderer(window, nullptr);
    if (r)
      SDL_SetRenderVSync(r, 1);
    return r;
  }

  SDL_Texture *LoadTexture(const std::string &path) {
    SDL_Texture *tex =
        IMG_LoadTexture(m_renderer, ("assets/bg/" + path).c_str());
    if (!tex)
      std::cerr << "Failed to load bg: " << path << " - " << SDL_GetError()
                << '\n';
    return tex;
  }

  void ShowBackground(const std::string &f) {
    if (m_background)
      SDL_DestroyTexture(m_background);
    m_background = LoadTexture(f);
  }

  void ShowCharacter(const std::string &id, const std::string &) {
    HideCharacter(id);
    std::string path = "assets/characters/" + id + "_normal.png";
    SDL_Texture *tex = IMG_LoadTexture(m_renderer, path.c_str());
    if (tex)
      m_characters[id] = tex;
  }

  void HideCharacter(const std::string &id) {
    auto it = m_characters.find(id);
    if (it != m_characters.end()) {
      SDL_DestroyTexture(it->second);
      m_characters.erase(it);
    }
  }

  void Say(const std::string &speaker, const std::string &name,
           const std::string &text) {
    m_currentSpeaker = speaker;
    m_currentName = name;
    m_currentText = text;
    m_displayedChars = 0;
    m_typewriterTimer = 0.0f;
  }

  void Narrate(const std::string &text) { Say("", "Narrator", text); }

  SDL_Texture *CreateSolidTexture(int w, int h, Uint8 r, Uint8 g, Uint8 b,
                                  Uint8 a) {
    SDL_Texture *tex = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBA8888,
                                         SDL_TEXTUREACCESS_TARGET, w, h);
    if (!tex)
      return nullptr;
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetRenderTarget(m_renderer, tex);
    SDL_SetRenderDrawColor(m_renderer, r, g, b, a);
    SDL_RenderClear(m_renderer);
    SDL_SetRenderTarget(m_renderer, nullptr);
    return tex;
  }

  SDL_Texture *RenderText(const std::string &text, SDL_Color color) {
    if (text.empty() || !m_font)
      return nullptr;
    SDL_Surface *surf =
        TTF_RenderText_Blended(m_font, text.c_str(), text.size(), color);
    if (!surf)
      return nullptr;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(m_renderer, surf);
    SDL_DestroySurface(surf);
    return tex;
  }
  void LoadScript(const std::string &filename) {
    auto result = m_lua.safe_script_file(
        filename, [](lua_State *, sol::protected_function_result pfr) {
          sol::error err = pfr;
          std::cerr << "Lua Script Error: " << err.what() << '\n';
          return pfr;
        });

    if (!result.valid()) {
      std::cerr << "Failed to load script: " << filename << '\n';
    } else {
      m_scriptFinished = false;
    }
  }

  void Reset() {
    m_currentText.clear();
    m_displayedChars = 0;
    m_typewriterTimer = 0.0f;
    // Optionally reset other state like characters, background, etc.
    m_currentSpeaker.clear();
    m_currentName.clear();
    if (m_background) {
      SDL_DestroyTexture(m_background);
      m_background = nullptr;
    }
    m_characters.clear();
  }
};

void init() {
  if (SDL_WasInit(SDL_INIT_VIDEO)) {
    std::cout << "Could not initialize SDL_video: " << SDL_GetError();
  }
  if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0) {
    std::cout << "Could not initialize SDL_video: " << SDL_GetError();
  }
}

VNEngine::VNEngine() : pImpl(new Impl()) {}

VNEngine::~VNEngine() { delete pImpl; }

bool VNEngine::Init(const char *title, int w, int h, bool fullscreen) {
  return pImpl->Init(title, w, h, fullscreen);
}

void VNEngine::ShutDown() { pImpl->ShutDown(); }

bool VNEngine::PollEvent(VNEvent &e) { return pImpl->PollEvent(e); }

void VNEngine::Present() { pImpl->Present(); }

int VNEngine::Width() const { return pImpl->m_screenWidth; }

int VNEngine::Height() const { return pImpl->m_screenHeight; }

void VNEngine::LoadScript(const std::string &filename) {
  pImpl->LoadScript(filename);
}

void VNEngine::Reset() { pImpl->Reset(); }

void VNEngine::HandleEvent(const VNEvent &e) { pImpl->HandleEvent(e); }

void VNEngine::Update(float dt) { pImpl->Update(dt); }

void VNEngine::Draw() { pImpl->Draw(); }

bool VNEngine::IsFinished() const { return pImpl->m_scriptFinished; }
