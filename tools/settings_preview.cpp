// settings_preview renders the settings window off-screen, to a PPM image, with no X-Plane: the same draw_settings
// the plugin runs, rasterised in software (the renderer is zoal-atc's tools/panel_preview.cpp).
//
//   settings_preview <fonts-dir> <out.ppm> [tab 0-4] [graphite|daylight|night] [width height] [scale]
//                    [mouse_x mouse_y]
#include "gui/kit.h"
#include "gui/settings_view.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "imgui.h"

namespace {

struct Image {
  int width = 0;
  int height = 0;
  std::vector<float> rgba; // premultiplied? no: straight, composited as we go

  void resize(int w, int h) {
    width = w;
    height = h;
    rgba.assign(static_cast<std::size_t>(w * h * 4), 0.0f);
  }
};

std::map<ImTextureID, ImTextureData *> g_textures;
ImTextureID g_next_texture = 1;

void update_textures(ImDrawData *data) {
  if (data->Textures == nullptr) {
    return;
  }
  for (ImTextureData *texture : *data->Textures) {
    if (texture->Status == ImTextureStatus_WantCreate ||
        texture->Status == ImTextureStatus_WantUpdates) {
      if (texture->TexID == ImTextureID_Invalid) {
        texture->SetTexID(g_next_texture++);
      }
      g_textures[texture->TexID] = texture;
      texture->SetStatus(ImTextureStatus_OK);
    } else if (texture->Status == ImTextureStatus_WantDestroy &&
               texture->UnusedFrames > 0) {
      g_textures.erase(texture->TexID);
      texture->SetTexID(ImTextureID_Invalid);
      texture->SetStatus(ImTextureStatus_Destroyed);
    }
  }
}

void sample(const ImTextureData *t, float u, float v, float out[4]) {
  if (t == nullptr) {
    out[0] = out[1] = out[2] = out[3] = 1.0f;
    return;
  }
  const float x = u * static_cast<float>(t->Width) - 0.5f;
  const float y = v * static_cast<float>(t->Height) - 0.5f;
  const int x0 = static_cast<int>(std::floor(x));
  const int y0 = static_cast<int>(std::floor(y));
  const float fx = x - static_cast<float>(x0);
  const float fy = y - static_cast<float>(y0);
  const auto *pixels = static_cast<const unsigned char *>(t->Pixels);
  auto at = [&](int px, int py, int c) {
    px = std::clamp(px, 0, t->Width - 1);
    py = std::clamp(py, 0, t->Height - 1);
    return static_cast<float>(pixels[(py * t->Width + px) * 4 + c]) / 255.0f;
  };
  for (int c = 0; c < 4; ++c) {
    const float top = at(x0, y0, c) * (1 - fx) + at(x0 + 1, y0, c) * fx;
    const float bottom = at(x0, y0 + 1, c) * (1 - fx) + at(x0 + 1, y0 + 1, c) * fx;
    out[c] = top * (1 - fy) + bottom * fy;
  }
}

void render(const ImDrawData *data, Image &image, float scale) {
  for (const ImDrawList *list : data->CmdLists) {
    const ImDrawVert *vertices = list->VtxBuffer.Data;
    const ImDrawIdx *indices = list->IdxBuffer.Data;
    for (const ImDrawCmd &cmd : list->CmdBuffer) {
      if (cmd.UserCallback != nullptr) {
        continue;
      }
      const auto found = g_textures.find(cmd.GetTexID());
      const ImTextureData *texture =
          found == g_textures.end() ? nullptr : found->second;
      const int clip_x0 = std::max(0, static_cast<int>(cmd.ClipRect.x * scale));
      const int clip_y0 = std::max(0, static_cast<int>(cmd.ClipRect.y * scale));
      const int clip_x1 =
          std::min(image.width, static_cast<int>(std::ceil(cmd.ClipRect.z * scale)));
      const int clip_y1 =
          std::min(image.height, static_cast<int>(std::ceil(cmd.ClipRect.w * scale)));
      for (unsigned i = 0; i + 2 < cmd.ElemCount; i += 3) {
        const ImDrawVert &a = vertices[cmd.VtxOffset + indices[cmd.IdxOffset + i]];
        const ImDrawVert &b = vertices[cmd.VtxOffset + indices[cmd.IdxOffset + i + 1]];
        const ImDrawVert &c = vertices[cmd.VtxOffset + indices[cmd.IdxOffset + i + 2]];
        const float ax = a.pos.x * scale, ay = a.pos.y * scale;
        const float bx = b.pos.x * scale, by = b.pos.y * scale;
        const float cx = c.pos.x * scale, cy = c.pos.y * scale;
        const float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
        if (std::fabs(area) < 1e-6f) {
          continue;
        }
        const int x0 = std::max(clip_x0, static_cast<int>(std::floor(std::min({ax, bx, cx}))));
        const int y0 = std::max(clip_y0, static_cast<int>(std::floor(std::min({ay, by, cy}))));
        const int x1 = std::min(clip_x1, static_cast<int>(std::ceil(std::max({ax, bx, cx}))));
        const int y1 = std::min(clip_y1, static_cast<int>(std::ceil(std::max({ay, by, cy}))));
        auto colour = [](ImU32 col, int shift) {
          return static_cast<float>((col >> shift) & 0xFF) / 255.0f;
        };
        for (int py = y0; py < y1; ++py) {
          for (int px = x0; px < x1; ++px) {
            const float sx = static_cast<float>(px) + 0.5f;
            const float sy = static_cast<float>(py) + 0.5f;
            const float w0 = ((bx - sx) * (cy - sy) - (by - sy) * (cx - sx)) / area;
            const float w1 = ((cx - sx) * (ay - sy) - (cy - sy) * (ax - sx)) / area;
            const float w2 = 1.0f - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) {
              continue;
            }
            const float u = w0 * a.uv.x + w1 * b.uv.x + w2 * c.uv.x;
            const float v = w0 * a.uv.y + w1 * b.uv.y + w2 * c.uv.y;
            float tex[4];
            sample(texture, u, v, tex);
            float src[4];
            const int shifts[4] = {IM_COL32_R_SHIFT, IM_COL32_G_SHIFT,
                                   IM_COL32_B_SHIFT, IM_COL32_A_SHIFT};
            for (int k = 0; k < 4; ++k) {
              src[k] = (w0 * colour(a.col, shifts[k]) + w1 * colour(b.col, shifts[k]) +
                        w2 * colour(c.col, shifts[k])) *
                       tex[k];
            }
            float *dst = &image.rgba[static_cast<std::size_t>((py * image.width + px) * 4)];
            const float alpha = src[3];
            for (int k = 0; k < 3; ++k) {
              dst[k] = src[k] * alpha + dst[k] * (1.0f - alpha);
            }
            dst[3] = alpha + dst[3] * (1.0f - alpha);
          }
        }
      }
    }
  }
}

// Behind the glass, a sky: so the panel's translucency shows as it would in
// the sim.
void paint_sky(Image &image) {
  for (int y = 0; y < image.height; ++y) {
    const float t = static_cast<float>(y) / static_cast<float>(image.height);
    for (int x = 0; x < image.width; ++x) {
      float *px = &image.rgba[static_cast<std::size_t>((y * image.width + x) * 4)];
      const float band = (x / 160 + y / 160) % 2 == 0 ? 0.03f : 0.0f;
      px[0] = 0.36f + 0.30f * t + band;
      px[1] = 0.52f + 0.22f * t + band;
      px[2] = 0.78f + 0.08f * t + band;
      px[3] = 1.0f;
    }
  }
}

bool write_ppm(const Image &image, const std::string &path) {
  std::ofstream out(path, std::ios::binary);
  out << "P6\n" << image.width << " " << image.height << "\n255\n";
  for (std::size_t i = 0; i < image.rgba.size(); i += 4) {
    for (int k = 0; k < 3; ++k) {
      const float v = std::clamp(image.rgba[i + static_cast<std::size_t>(k)], 0.0f, 1.0f);
      out.put(static_cast<char>(std::lround(std::pow(v, 1.0f) * 255.0f)));
    }
  }
  return static_cast<bool>(out);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: settings_preview <fonts-dir> <out.ppm> [tab 0-4] [graphite|daylight|night] "
                     "[width height] [scale] [mouse_x mouse_y]\n";
        return 2;
    }
    const std::string fonts = argv[1], out = argv[2];
    const int tab = argc > 3 ? std::atoi(argv[3]) : 0;
    const std::string theme = argc > 4 ? argv[4] : "graphite";
    const int width = argc > 6 ? std::atoi(argv[5]) : 780;
    const int height = argc > 6 ? std::atoi(argv[6]) : 680;
    const float scale = argc > 7 ? float(std::atof(argv[7])) : 1.0f;
    const float mouse_x = argc > 9 ? float(std::atof(argv[8])) : -1.0f;
    const float mouse_y = argc > 9 ? float(std::atof(argv[9])) : -1.0f;

    zg::Settings settings;
    settings.ui_theme = theme;
    zg::gui::SettingsView view;
    view.host.version = "v0.0.1-alpha.6";
    view.host.get = [&] { return settings; };
    view.host.apply = [&](zg::Settings s) { settings = s; };
    view.host.status = [] {
        zg::gui::LiveStatus st;
        st.enabled = true;
        st.style = "summer";
        st.biome = "temperate";
        st.koppen = "Cfb";
        st.instances = 2316;
        st.airports = 1;
        return st;
    };
    view.shown = view.saved = settings;
    view.live = view.host.status();
    view.tab = tab;

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.Fonts->TexDesiredFormat = ImTextureFormat_RGBA32;
    io.DisplaySize = ImVec2(float(width), float(height));
    io.DisplayFramebufferScale = ImVec2(scale, scale);
    view.face = zg::ui::load_typeface(fonts, fonts, [](const std::string& line) { std::cerr << line << "\n"; });
    zg::gui::settings_setup_style();

    Image image;
    for (int i = 0; i < 4; ++i) {
        io.DeltaTime = 1.0f / 60.0f;
        io.AddMousePosEvent(mouse_x, mouse_y);
        ImGui::NewFrame();
        zg::gui::draw_settings(view);
        ImGui::Render();
        update_textures(ImGui::GetDrawData());
        view.later.clear();
    }
    image.resize(int(float(width) * scale), int(float(height) * scale));
    paint_sky(image);
    render(ImGui::GetDrawData(), image, scale);
    ImGui::DestroyContext();
    if (!write_ppm(image, out)) {
        std::cerr << "could not write " << out << "\n";
        return 1;
    }
    return 0;
}
