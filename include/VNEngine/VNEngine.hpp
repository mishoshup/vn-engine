// VNEngine/VNEngine.hpp
#pragma once

#include <string>

struct VNEvent {
  enum Type { Quit, KeyDown, MouseDown, Unknown };
  Type type = Unknown;
  unsigned int key = 0;
};

class VNEngine {
public:
  VNEngine();
  ~VNEngine();

  bool Init(const char *title, int w, int h, bool fullscreen = false);
  void ShutDown();

  bool PollEvent(VNEvent &e);
  void Present();

  // Public accessors (no SDL types exposed)
  int Width() const;
  int Height() const;

  void LoadScript(const std::string &filename);
  void Reset();
  void HandleEvent(const VNEvent &e);
  void Update(float dt);
  void Draw();

  bool IsFinished() const;

private:
  class Impl;
  Impl *pImpl;
};
