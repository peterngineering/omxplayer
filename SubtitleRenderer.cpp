/*
 *
 *    Copyright (C) 2020 Michael J. Walsh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#include <string>
#include <vector>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <cairo/cairo.h>
#include <cairo/cairo-ft.h>

#include "utils/RegExp.h"
#include "SubtitleRenderer.h"
#include "OMXPlayerSubtitles.h"
#include "DispmanxLayer.h"
#include "Subtitle.h"

using namespace std;

// A fairly unscientific survey showed that with a font size of 59px subtitles lines
// were rarely longer than 1300px. We also assume that larger font sizes (frequently used
// in East Asian scripts) would result in shorter not longer subtitles.
#define MAX_SUBTITLE_LINE_WIDTH 1300

SubtitleRenderer::SubtitleRenderer(OMXSubConfig *config)
: m_centered(config->centered),
  m_ghost_box(config->ghost_box),
  m_max_lines(config->subtitle_lines),
  m_font_size(config->font_size)
{
  // Subtitle tag parser regexes
  m_tags = new CRegExp("(\\n|<[^>]*>|\\{\\\\[^\\}]*\\})");
  m_font_color_html = new CRegExp("color[ \\t]*=[ \\t\"']*#?([a-f0-9]{6})");
  m_font_color_curly = new CRegExp("^\\{\\\\c&h([a-f0-9]{2})([a-f0-9]{2})([a-f0-9]{2})&\\}$");

  // import font files
  if(FT_Init_FreeType(&m_ft_library) != 0)
    throw "Failed to initiate FreeType";

  if(FT_New_Face(m_ft_library, config->reg_font, 0, &m_ft_face_normal) != 0)
    throw "Failed to load font";

  if(FT_New_Face(m_ft_library, config->italic_font, 0, &m_ft_face_italic) != 0)
    throw "Failed to load font";

  if(FT_New_Face(m_ft_library, config->bold_font, 0, &m_ft_face_bold) != 0)
    throw "Failed to load font";

  // font colours
  m_ghost_box_transparency = cairo_pattern_create_rgba(0.0f, 0.0f, 0.0f, 0.5f);
  m_default_font_color = cairo_pattern_create_rgba(0.866667, 0.866667, 0.866667, 1.0f);
  m_black_font_outline = cairo_pattern_create_rgba(0.0f, 0.0f, 0.0f, 1.0f);

  InitSubLayer();
}

  /*    *    *    *     *    *    *    *    *    *    *    *
   * Set up layer for text subtitles and on screen display *
   *    *    *    *     *    *    *    *    *    *    *    */
void SubtitleRenderer::InitSubLayer()
{
  // free existing layer and scaled fonts (if any)
  if (subtitleLayer) delete subtitleLayer;
  if (m_scaled_font[NORMAL_FONT]) cairo_scaled_font_destroy(m_scaled_font[NORMAL_FONT]);
  if (m_scaled_font[ITALIC_FONT]) cairo_scaled_font_destroy(m_scaled_font[ITALIC_FONT]);
  if (m_scaled_font[BOLD_FONT]) cairo_scaled_font_destroy(m_scaled_font[BOLD_FONT]);

  // Determine screen size
  const Rect &screen = DispmanxLayer::GetScreenDimensions();

  // Calculate font as thousands of screen height
  m_scaled_font_size = screen.height * m_font_size;

  // Calculate padding as 1/4 of the font size
  m_padding = m_scaled_font_size / 4;

  // And line_height combines the two
  int line_height = m_scaled_font_size + m_padding;

  // Calculate image dimensions - must be evenly divisible by 16
  Rect text_subtitle_rect;
  text_subtitle_rect.height = (m_max_lines * line_height + 20) & ~15; // grow to fit
  text_subtitle_rect.y = screen.y + screen.height - text_subtitle_rect.height - (line_height / 2);

  if(m_centered) {
    text_subtitle_rect.width = (screen.width - 100) & ~15; // shrink to fit
    text_subtitle_rect.x = screen.x + (screen.width - text_subtitle_rect.width) / 2; // centered on screen
  } else {
    if(screen.width > MAX_SUBTITLE_LINE_WIDTH) {
      text_subtitle_rect.x = screen.x + (screen.width - MAX_SUBTITLE_LINE_WIDTH) / 2;
    } else if(screen.width > screen.height) {
      text_subtitle_rect.x = screen.x + (screen.width - screen.height) / 2;
    } else {
      text_subtitle_rect.x = screen.x + 100;
    }

    text_subtitle_rect.width = (screen.width - 100 - text_subtitle_rect.x) & ~15; // shrink to fit
  }

  // Create layer
  subtitleLayer = new DispmanxLayer(4, text_subtitle_rect);

  // prepare un-scaled fonts
  cairo_font_face_t *normal_font = cairo_ft_font_face_create_for_ft_face(m_ft_face_normal, 0);
  cairo_font_face_t *italic_font = cairo_ft_font_face_create_for_ft_face(m_ft_face_italic, 0);
  cairo_font_face_t *bold_font = cairo_ft_font_face_create_for_ft_face(m_ft_face_bold, 0);

  // prepare scaled fonts
  cairo_matrix_t sizeMatrix, ctm;
  cairo_matrix_init_identity(&ctm);
  cairo_matrix_init_scale(&sizeMatrix, m_scaled_font_size, m_scaled_font_size);
  cairo_font_options_t *options = cairo_font_options_create();

  m_scaled_font[NORMAL_FONT] = cairo_scaled_font_create(normal_font, &sizeMatrix, &ctm, options);
  m_scaled_font[ITALIC_FONT] = cairo_scaled_font_create(italic_font, &sizeMatrix, &ctm, options);
  m_scaled_font[BOLD_FONT] = cairo_scaled_font_create(bold_font, &sizeMatrix, &ctm, options);

  // cleanup
  cairo_font_options_destroy(options);
  cairo_font_face_destroy(normal_font);
  cairo_font_face_destroy(italic_font);
  cairo_font_face_destroy(bold_font);
}

  /*    *    *    *    *    *    *    *    *    *    *    *
   *            Set up layer for DVD subtitles            *
   *    *    *    *    *    *    *    *    *    *    *    */

void SubtitleRenderer::SetDVDSubtitleLayer(DispmanxLayer *dl)
{
  if(dvdSubLayer)
    delete dvdSubLayer;

  // set layer
  dvdSubLayer = dl;
}

void SubtitleRenderer::SetFont(int *old_font, int new_font)
{
  if(new_font == *old_font) return;

  cairo_set_scaled_font(m_cr, m_scaled_font[new_font]);
  *old_font = new_font;
}

void SubtitleRenderer::SetColor(unsigned int *old_color, unsigned int new_color)
{
  if(new_color == *old_color) return;

  if(new_color == FC_OFF_WHITE)
    cairo_set_source(m_cr, m_default_font_color);
  else if(new_color == FC_GHOST)
    cairo_set_source(m_cr, m_ghost_box_transparency);
  else if(new_color == FC_BLACK)
    cairo_set_source(m_cr, m_black_font_outline);
  else {
    float r = ((new_color >> 16) & 0xFF) / 255.0f;
    float g = ((new_color >>  8) & 0xFF) / 255.0f;
    float b = ((new_color >>  0) & 0xFF) / 255.0f;

    cairo_set_source_rgba(m_cr, r, g, b, 1.0f);
  }

  *old_color = new_color;
}


void SubtitleRenderer::Prepare(Subtitle &sub)
{
  Unprepare();

  if(sub.isImage)
    MakeSubtitleImage(sub);
  else
    ParseLines(sub.text);
}

void SubtitleRenderer::Prepare(const string &lines)
{
  Unprepare();

  ParseLines(lines);
}

void SubtitleRenderer::MakeSubtitleImage(vector<vector<SubtitleText> > &parsed_lines)
{
  // create surface
  m_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, subtitleLayer->GetSourceWidth(), subtitleLayer->GetSourceHeight());
  if(cairo_surface_status(m_surface) != CAIRO_STATUS_SUCCESS)
    throw "Failed to create cairo surface";

  m_cr = cairo_create(m_surface);
  if(cairo_status(m_cr) != CAIRO_STATUS_SUCCESS)
    throw "Failed to create cairo object";

  // Reset font control vars
  int font = UNSET_FONT;
  unsigned int color = FC_NOT_SET;

  // cursor y position
  int cursor_y_position = subtitleLayer->GetSourceHeight() - m_padding;

  // Limit the number of line
  int no_of_lines = parsed_lines.size();
  if(no_of_lines > m_max_lines) no_of_lines = m_max_lines;

  for(int i = no_of_lines - 1; i > -1; i--) {
    int box_width = (m_padding * 2);
    int text_parts = parsed_lines[i].size();

    // cursor x position
    int cursor_x_position = 0;

    for(int j = 0; j < text_parts; j++) {
      // prepare font glyphs
      if(cairo_scaled_font_text_to_glyphs(
          m_scaled_font[parsed_lines[i][j].font],
          cursor_x_position + m_padding,
          cursor_y_position - (m_padding / 4),
          parsed_lines[i][j].text.data(),
          parsed_lines[i][j].text.length(),
          &parsed_lines[i][j].glyphs,
          &parsed_lines[i][j].num_glyphs,
          nullptr, nullptr, nullptr) != CAIRO_STATUS_SUCCESS)
      {
        printf("Failed: %s\n", parsed_lines[i][j].text.c_str());
        throw "cairo_scaled_font_text_to_glyphs failed";
      }

      // calculate font extents
      cairo_text_extents_t extents;
      cairo_scaled_font_glyph_extents(
        m_scaled_font[parsed_lines[i][j].font],
        parsed_lines[i][j].glyphs,
        parsed_lines[i][j].num_glyphs,
        &extents);

      cursor_x_position += extents.x_advance;
      box_width += extents.x_advance;
    }

    // aligned text
    if(m_centered) {
      cursor_x_position = (subtitleLayer->GetSourceWidth() / 2) - (box_width / 2);

      for(int j = 0; j < text_parts; j++) {
        cairo_glyph_t *p = parsed_lines[i][j].glyphs;
        for(int h = 0; h < parsed_lines[i][j].num_glyphs; h++, p++) {
          p->x += cursor_x_position;
        }
      }
    } else {
      cursor_x_position = 0;
    }

    // draw ghost box
    if(m_ghost_box) {
      SetColor(&color, FC_GHOST);
      cairo_rectangle(m_cr, cursor_x_position, cursor_y_position - m_scaled_font_size, box_width,
        m_scaled_font_size + m_padding);
      cairo_fill(m_cr);
    }

    for(int j = 0; j < text_parts; j++) {
      SetFont(&font, parsed_lines[i][j].font);
      SetColor(&color, parsed_lines[i][j].color);

      // draw text
      cairo_glyph_path(m_cr, parsed_lines[i][j].glyphs, parsed_lines[i][j].num_glyphs);

      // free glyph array
      cairo_glyph_free(parsed_lines[i][j].glyphs);
      parsed_lines[i][j].glyphs = nullptr;
    }

    // draw black text outline
    cairo_fill_preserve(m_cr);
    SetColor(&color, FC_BLACK);
    cairo_set_line_width(m_cr, 2);
    cairo_stroke(m_cr);

    // next line
    cursor_y_position -= m_scaled_font_size + m_padding;
  }

  m_cairo_image_data = cairo_image_surface_get_data(m_surface);
}


void SubtitleRenderer::MakeSubtitleImage(Subtitle &sub)
{
  unsigned char *p;

  // Subtitles which exceed dimensions are ignored
  if(sub.image.rect.x + sub.image.rect.width  > dvdSubLayer->GetSourceWidth() || sub.image.rect.y + sub.image.rect.height  > dvdSubLayer->GetSourceHeight())
    return;

  p = m_bitmap_image_data = new unsigned char[dvdSubLayer->GetSourceWidth() * dvdSubLayer->GetSourceHeight()];

  auto SetBlank = [&p](int num_pixels)
  {
    memset(p, 0, num_pixels);
    p += num_pixels;
  };

  auto CopyPixels = [&p](const unsigned char *pixel, int len)
  {
    memcpy(p, pixel, len);
    p += len;
  };

  int right_padding  = dvdSubLayer->GetSourceWidth()  - sub.image.rect.width  - sub.image.rect.x;
  int bottom_padding = dvdSubLayer->GetSourceHeight() - sub.image.rect.height - sub.image.rect.y;

  // blanks char at top
  SetBlank(sub.image.rect.y * dvdSubLayer->GetSourceWidth());

  for(int j = 0; j < sub.image.rect.height; j++) {
    SetBlank(sub.image.rect.x);
    CopyPixels(sub.image.data.data() + (j * sub.image.rect.width), sub.image.rect.width);
    SetBlank(right_padding);
  }

  // blanks char at bottom
  SetBlank(bottom_padding * dvdSubLayer->GetSourceWidth());
}

void SubtitleRenderer::ShowNext()
{
  if(m_bitmap_image_data) {
    subtitleLayer->HideElement();
    dvdSubLayer->SetImageData(m_bitmap_image_data);
    Unprepare();
  } else if(m_cairo_image_data) {
    if(dvdSubLayer) dvdSubLayer->HideElement();
    subtitleLayer->SetImageData(m_cairo_image_data);
    Unprepare();
  }
}

void SubtitleRenderer::Hide()
{
  subtitleLayer->HideElement();
  if(dvdSubLayer) dvdSubLayer->HideElement();
}

void SubtitleRenderer::Clear()
{
  subtitleLayer->ClearImage();
  if(dvdSubLayer)
    delete dvdSubLayer;
  dvdSubLayer = nullptr;
}

void SubtitleRenderer::Unprepare()
{
  if(m_bitmap_image_data) {
    delete[] m_bitmap_image_data;
    m_bitmap_image_data = nullptr;
  }

  if(m_cairo_image_data) {
    cairo_destroy(m_cr);
    cairo_surface_destroy(m_surface);
    m_cairo_image_data = nullptr;
  }
}

// Tag parser functions
void SubtitleRenderer::ParseLines(const string &text)
{
  vector<vector<SubtitleText> > formatted_lines(1);

  bool bold = false, italic = false;
  unsigned int color = FC_OFF_WHITE;

  int pos = 0, old_pos = 0;
  while (pos < (int)text.length()) {
    pos = m_tags->RegFind(text, pos);
    string fullTag = m_tags->GetMatch(0);

    //parse text
    if(pos != old_pos || fullTag == "\n") {
      int l = pos == -1 ? text.length() - old_pos : pos - old_pos;

      if(l > 0) {
        int font = italic ? ITALIC_FONT : (bold ? BOLD_FONT : NORMAL_FONT);
        formatted_lines.back().emplace_back(&text[old_pos], l, font, color);
      }
    }

    // No more tags found
    if(pos < 0) break;

    // convert to lower case
    for(auto &c : fullTag)
      if(c >= 'A' && c <= 'Z')
        c += 32;

    pos += fullTag.length();
    old_pos = pos;

    // Parse Tag
    if (fullTag == "\n" ) {
      formatted_lines.emplace_back();
    } else if (fullTag == "<b>" || fullTag == "{\\b1}") {
      bold = true;
    } else if (fullTag == "</b>" || fullTag == "{\\b0}") {
      bold = false;
    } else if (fullTag == "<i>" || fullTag == "{\\i1}") {
      italic = true;
    } else if (fullTag == "</i>" || fullTag == "{\\i0}") {
      italic = false;
    } else if (fullTag == "</font>" || fullTag == "{\\c}") {
      color = FC_OFF_WHITE;
    } else if (fullTag.substr(0,5) == "<font") {
      if(m_font_color_html->RegFind(fullTag, 5) >= 0) {
        color = Hex2int(m_font_color_html->GetMatch(1));
      }
    } else if(m_font_color_curly->RegFind(fullTag) >= 0) {
      string t = m_font_color_curly->GetMatch(3) +
                 m_font_color_curly->GetMatch(2) +
                 m_font_color_curly->GetMatch(1);

      color = Hex2int(t);
    }
  }

  MakeSubtitleImage(formatted_lines);
}

// expects 6 lowercase, digit hex string
unsigned int SubtitleRenderer::Hex2int(const string &hex)
{
  unsigned int r = 0;
  for(int i = 0, f = 20; i < 6; i++, f -= 4)
    if(hex[i] >= 'a')
      r += (hex[i] - 87) << f;
    else
      r += (hex[i] - 48) << f;

  return r;
}

SubtitleRenderer::~SubtitleRenderer()
{
  //destroy cairo surface, if defined
  Unprepare();

  // remove DispmanX layer
  delete subtitleLayer;
  if(dvdSubLayer) delete dvdSubLayer;

  // destroy cairo fonts
  for(int i = 0; i < 3; i++)
    cairo_scaled_font_destroy(m_scaled_font[i]);

  // and patterns
  cairo_pattern_destroy(m_ghost_box_transparency);
  cairo_pattern_destroy(m_default_font_color);
  cairo_pattern_destroy(m_black_font_outline);

  // and the free type stuff
  FT_Done_Face(m_ft_face_normal);
  FT_Done_Face(m_ft_face_italic);
  FT_Done_Face(m_ft_face_bold);

  // and free type library
  FT_Done_FreeType(m_ft_library);

  //delete regexes
  delete m_tags;
  delete m_font_color_html;
  delete m_font_color_curly;
}
