/*
 * Copyright (c) 2010-2026 OTClient <https://github.com/edubart/otclient>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "image.h"

#include "apngloader.h"
#include "framework/core/filestream.h"
#include "framework/core/resourcemanager.h"

using namespace qrcodegen;

Image::Image(const Size& size, const int bpp, const uint8_t* pixels) : m_size(size), m_bpp(bpp)
{
    m_pixels.resize(size.area() * bpp, 0);
    if (pixels)
        memcpy(&m_pixels[0], pixels, m_pixels.size());
}

ImagePtr Image::load(const std::string& file)
{
    const auto& path = g_resources.guessFilePath(file, "png");
    try {
        return loadPNG(path);
    } catch (const stdext::exception& e) {
        g_logger.error("Unable to load image '{}': {}", path, e.what());
    }
    return nullptr;
}

ImagePtr Image::loadPNG(const char* data, const size_t size)
{
    std::stringstream fin(std::string{ data, size });
    ImagePtr image;
    if (apng_data apng; load_apng(fin, &apng) == 0) {
        const size_t frameSize = static_cast<size_t>(apng.width) * apng.height * apng.bpp;
        const uint32_t availableFrames = apng.last_frame > apng.first_frame
                                             ? apng.last_frame - apng.first_frame
                                             : 0;
        const uint32_t frameCount = std::min(apng.num_frames, availableFrames);
        const uint32_t firstFrame = availableFrames > 0 ? apng.first_frame : 0;
        const auto* firstFrameData = apng.pdata + (static_cast<size_t>(firstFrame) * frameSize);
        image = std::make_shared<Image>(Size(apng.width, apng.height), apng.bpp, firstFrameData);
        if (frameCount > 1 && apng.frames_delay) {
            for (uint32_t i = 0; i < frameCount; ++i) {
                // Create a new Image for every frame to avoid circular reference (image -> m_animation -> image)
                const size_t frameOffset = static_cast<size_t>(apng.first_frame + i) * frameSize;
                ImagePtr frameImage = std::make_shared<Image>(Size(apng.width, apng.height), apng.bpp,
                                                              apng.pdata + frameOffset);
                image->addAnimationFrame(frameImage, apng.frames_delay[i]);
            }
        }
        free_apng(&apng);
    }

    if (!image)
        return nullptr;

    int cntTransparentPixel = 0;
    for (const auto& pixel : image->getPixels()) {
        if (pixel == 0 && ++cntTransparentPixel == 4) {
            image->setTransparentPixel(true);
            break;
        }
    }

    return image;
}

ImagePtr Image::loadPNG(const std::string& file)
{
    std::stringstream fin;
    g_resources.readFileStream(file, fin);

    const std::string buffer{ fin.str() };

    return loadPNG(buffer.data(), buffer.size());
}

void Image::savePNG(const std::string& fileName)
{
    const auto& fin = g_resources.createFile(fileName);
    if (!fin)
        throw Exception("failed to open file '{}' for write", fileName);

    fin->cache();
    std::stringstream data;
    save_png(data, m_size.width(), m_size.height(), 4, getPixelData());
    fin->write(data.str().c_str(), data.str().length());
    fin->flush();
    fin->close();
}

void Image::overwriteMask(const Color& maskedColor, const Color& insideColor, const Color& outsideColor)
{
    assert(m_bpp == 4);

    // En bytes crudos a proposito. Color guarda sus componentes en float y
    // recalcula un hash al construirse (color.h:40), asi que hacerlo con objetos
    // Color costaba cuatro divisiones y un empaquetado POR PIXEL. Esto corre para
    // las cuatro mascaras de cada fotograma de cada outfit, y la ventana de
    // outfits compone sus texturas en el hilo principal: era el tiron al abrirla.
    const uint8_t mask[4]{ maskedColor.r(), maskedColor.g(), maskedColor.b(), maskedColor.a() };
    const uint8_t inside[4]{ insideColor.r(), insideColor.g(), insideColor.b(), insideColor.a() };
    const uint8_t outside[4]{ outsideColor.r(), outsideColor.g(), outsideColor.b(), outsideColor.a() };

    for (uint8_t* px = m_pixels.data(), *end = px + static_cast<size_t>(getPixelCount()) * 4; px < end; px += 4) {
        const uint8_t* write = (px[0] == mask[0] && px[1] == mask[1] &&
                                px[2] == mask[2] && px[3] == mask[3]) ? inside : outside;
        px[0] = write[0];
        px[1] = write[1];
        px[2] = write[2];
        px[3] = write[3];
    }
}

void Image::overwrite(const Color& color)
{
    assert(m_bpp == 4);

    // Igual que overwriteMask: sin construir un Color por pixel. Los pixeles que
    // ya valen exactamente Color::alpha se quedan como estan, que es lo que hacia
    // la version anterior al reescribirlos con el mismo valor.
    const uint8_t transparent[4]{ Color::alpha.r(), Color::alpha.g(), Color::alpha.b(), Color::alpha.a() };
    const uint8_t solid[4]{ color.r(), color.g(), color.b(), color.a() };

    for (uint8_t* px = m_pixels.data(), *end = px + static_cast<size_t>(getPixelCount()) * 4; px < end; px += 4) {
        if (px[0] == transparent[0] && px[1] == transparent[1] &&
            px[2] == transparent[2] && px[3] == transparent[3])
            continue;

        px[0] = solid[0];
        px[1] = solid[1];
        px[2] = solid[2];
        px[3] = solid[3];
    }
}

void Image::blit(const Point& dest, const ImagePtr& other)
{
    assert(m_bpp == 4);

    if (!other)
        return;

    // El recorte a los limites del destino se calcula UNA vez y luego se copia por
    // filas. Sin recorte, un blit que se salga escribe fuera del vector y corrompe
    // el heap; la version anterior lo comprobaba pixel a pixel y ademas deducia x
    // e y con una division y un modulo en cada uno.
    const int ow = other->getWidth();
    const int oh = other->getHeight();

    const int x0 = std::max<int>(0, -dest.x);
    const int y0 = std::max<int>(0, -dest.y);
    const int x1 = std::min<int>(ow, m_size.width() - dest.x);
    const int y1 = std::min<int>(oh, m_size.height() - dest.y);
    if (x0 >= x1 || y0 >= y1)
        return;

    const uint8_t* otherPixels = other->getPixelData();

    for (int y = y0; y < y1; ++y) {
        const uint8_t* src = otherPixels + (static_cast<size_t>(y) * ow + x0) * 4;
        uint8_t* dst = m_pixels.data() +
            (static_cast<size_t>(dest.y + y) * m_size.width() + dest.x + x0) * 4;

        for (int x = x0; x < x1; ++x, src += 4, dst += 4) {
            if (src[3] == 0)
                continue;               // los pixeles transparentes no pisan el destino

            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst[3] = src[3];
        }
    }
}

void Image::paste(const ImagePtr& other)
{
    assert(m_bpp == 4);

    if (!other)
        return;

    const uint8_t* otherPixels = other->getPixelData();
    for (int p = 0; p < other->getPixelCount(); ++p) {
        const int x = p % other->getWidth();
        const int y = p / other->getWidth();
        const int pos = (y * m_size.width() + x) * 4;

        m_pixels[pos + 0] = otherPixels[p * 4 + 0];
        m_pixels[pos + 1] = otherPixels[p * 4 + 1];
        m_pixels[pos + 2] = otherPixels[p * 4 + 2];
        m_pixels[pos + 3] = otherPixels[p * 4 + 3];
    }
}

bool Image::nextMipmap()
{
    assert(m_bpp == 4);

    const int iw = m_size.width();
    const int ih = m_size.height();
    if ((iw == 1 && ih == 1) || m_pixels.empty())
        return false;

    const int ow = iw > 1 ? iw / 2 : 1;
    const int oh = ih > 1 ? ih / 2 : 1;

    std::vector<uint8_t > pixels(ow * oh * 4, 0xFF);

    //FIXME: calculate mipmaps for 8x1, 4x1, 2x1 ...
    if (iw != 1 && ih != 1) {
        for (int x = 0; x < ow; ++x) {
            for (int y = 0; y < oh; ++y) {
                uint8_t* inPixel[4];
                inPixel[0] = &m_pixels[((y * 2) * iw + (x * 2)) * 4];
                inPixel[1] = &m_pixels[((y * 2) * iw + (x * 2) + 1) * 4];
                inPixel[2] = &m_pixels[((y * 2 + 1) * iw + (x * 2)) * 4];
                inPixel[3] = &m_pixels[((y * 2 + 1) * iw + (x * 2) + 1) * 4];
                uint8_t* outPixel = &pixels[(y * ow + x) * 4];

                int pixelsSum[4];
                for (int& i : pixelsSum)
                    i = 0;

                int usedPixels = 0;
                for (auto& j : inPixel) {
                    // ignore colors of complete alpha pixels
                    if (j[3] < 16)
                        continue;

                    for (int i = 0; i < 4; ++i)
                        pixelsSum[i] += j[i];

                    ++usedPixels;
                }

                // try to guess the alpha pixel more accurately
                for (int i = 0; i < 4; ++i) {
                    if (usedPixels > 0)
                        outPixel[i] = pixelsSum[i] / usedPixels;
                    else
                        outPixel[i] = 0;
                }
                outPixel[3] = pixelsSum[3] / 4;
            }
        }
    }

    m_pixels = pixels;
    m_size = { ow, oh };
    return true;
}

void Image::flipVertically()
{
    for (int line = 0, h = m_size.height(), w = m_size.width(); line != h / 2; ++line) {
        std::swap_ranges(
            m_pixels.begin() + 4 * w * line,
            m_pixels.begin() + 4 * w * (line + 1),
            m_pixels.begin() + 4 * w * (h - line - 1));
    }
}

void Image::setOpacity(const uint8_t v) {
    for (size_t i = 3, s = m_pixels.size(); i < s; i += 4)
        m_pixels[i] = v;
}

void Image::reverseChannels()
{
    uint8_t* pixelData = m_pixels.data();
    for (uint8_t* itr = pixelData; itr < pixelData + m_pixels.size(); itr += m_bpp) {
        std::swap(*(itr + 0), *(itr + 2));
    }
}

ImagePtr Image::fromQRCode(const std::string& code, const int border)
{
    try {
        const QrCode qrCode = QrCode::encodeText(code.c_str(), QrCode::Ecc::MEDIUM);

        const auto size = qrCode.getSize();
        ImagePtr image(new Image(Size(size + border * 2, size + border * 2)));

        for (int x = 0; x < size + border * 2; ++x) {
            for (int y = 0; y < size + border * 2; ++y) {
                image->setPixel(x, y, qrCode.getModule(x - border, y - border) ? Color::black : Color::white);
            }
        }

        return image;
    } catch (const std::exception& e) {
        g_logger.error("Failed to encode qr-code: '{}': {}", code, e.what());
    }

    return {};
}
