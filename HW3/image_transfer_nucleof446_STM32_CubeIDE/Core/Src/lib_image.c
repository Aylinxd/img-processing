/*
 * lib_image.c
  * @brief  Initialize the image structure with required information
  * @param  img     Pointer to image structure
  * @param  pImg    Pointer to image buffer
  * @param  height  Height of the image in pixels
  * @param  width   Width of the image in pixels
  * @param  format  Choose IMAGE_FORMAT_GRAYSCALE, IMAGE_FORMAT_RGB565, or IMAGE_FORMAT_RGB888
  * @retval 0 if successfully initialized, -1 otherwise

 */
#include <string.h>
#include <stdint.h>
#include "lib_image.h"

static uint8_t Otsu_FromHist256(const uint32_t hist[256], uint32_t total);


#define __LIB_IMAGE_CHECK_PARAM(param)				{if(param == 0) return IMAGE_ERROR;}


static inline uint8_t _px_get0(const uint8_t *p, int x, int y, int w, int h)
{
    if ((unsigned)x >= (unsigned)w || (unsigned)y >= (unsigned)h) return 0;
    return p[y*w + x];
}


void IMAGE_Dilate3x3(const IMAGE_HandleTypeDef *src, IMAGE_HandleTypeDef *dst)
{
    if (!src || !dst || !src->pData || !dst->pData) return;
    if (src->format != IMAGE_FORMAT_GRAYSCALE || dst->format != IMAGE_FORMAT_GRAYSCALE) return;
    if (src->width != dst->width || src->height != dst->height) return;

    const int w = src->width;
    const int h = src->height;
    const uint8_t *in = src->pData;
    uint8_t *out = dst->pData;

    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            uint8_t any = 0;

            for (int j = -1; j <= 1 && !any; j++)
            {
                for (int i = -1; i <= 1; i++)
                {
                    if (_px_get0(in, x+i, y+j, w, h) == 255) { any = 1; break; }
                }
            }
            out[y*w + x] = any ? 255 : 0;
        }
    }
}

// dst = erode(src)   (3x3)
// Sınır dışı 0 sayıldığı için kenarlar daha kolay erozyona uğrar .
void IMAGE_Erode3x3(const IMAGE_HandleTypeDef *src, IMAGE_HandleTypeDef *dst)
{
    if (!src || !dst || !src->pData || !dst->pData) return;
    if (src->format != IMAGE_FORMAT_GRAYSCALE || dst->format != IMAGE_FORMAT_GRAYSCALE) return;
    if (src->width != dst->width || src->height != dst->height) return;

    const int w = src->width;
    const int h = src->height;
    const uint8_t *in = src->pData;
    uint8_t *out = dst->pData;

    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            uint8_t all = 1;
            // 3x3'te hepsi 255 ise -> 255, yoksa 0
            for (int j = -1; j <= 1 && all; j++)
            {
                for (int i = -1; i <= 1; i++)
                {
                    if (_px_get0(in, x+i, y+j, w, h) != 255) { all = 0; break; }
                }
            }
            out[y*w + x] = all ? 255 : 0;
        }
    }
}

// opening = erosion -> dilation
void IMAGE_Opening3x3(const IMAGE_HandleTypeDef *src, IMAGE_HandleTypeDef *dst, uint8_t *scratch)
{
    if (!src || !dst || !scratch) return;
    IMAGE_HandleTypeDef tmp = *dst;
    tmp.pData = scratch;

    IMAGE_Erode3x3(src, &tmp);
    IMAGE_Dilate3x3(&tmp, dst);
}

// closing = dilation -> erosion
void IMAGE_Closing3x3(const IMAGE_HandleTypeDef *src, IMAGE_HandleTypeDef *dst, uint8_t *scratch)
{
    if (!src || !dst || !scratch) return;
    IMAGE_HandleTypeDef tmp = *dst;
    tmp.pData = scratch;

    IMAGE_Dilate3x3(src, &tmp);
    IMAGE_Erode3x3(&tmp, dst);
}

static uint8_t Otsu_FromHist256(const uint32_t hist[256], uint32_t total)
{
    float sum = 0.0f;
    for (int i = 0; i < 256; i++)
        sum += (float)i * (float)hist[i];

    float sumB = 0.0f;
    uint32_t wB = 0;
    float varMax = 0.0f;
    uint8_t threshold = 0;

    for (int t = 0; t < 256; t++)
    {
        wB += hist[t];
        if (wB == 0) continue;

        uint32_t wF = total - wB;
        if (wF == 0) break;

        sumB += (float)t * (float)hist[t];

        float mB = sumB / (float)wB;
        float mF = (sum - sumB) / (float)wF;

        float diff = mB - mF;
        float varBetween = (float)wB * (float)wF * diff * diff;

        if (varBetween > varMax)
        {
            varMax = varBetween;
            threshold = (uint8_t)t;
        }
    }

    return threshold;
}

int8_t LIB_IMAGE_InitStruct(IMAGE_HandleTypeDef * img, uint8_t *pImg, uint16_t height, uint16_t width, IMAGE_Format format)
{
	__LIB_IMAGE_CHECK_PARAM(img);
	__LIB_IMAGE_CHECK_PARAM(pImg);
	__LIB_IMAGE_CHECK_PARAM(format);
	__LIB_IMAGE_CHECK_PARAM(width);
	__LIB_IMAGE_CHECK_PARAM(height);
	img->format = format;
	img->height = height;
	img->width 	= width;
	img->pData 	= pImg;
	img->size 	= (uint32_t)img->format * (uint32_t)img->height * (uint32_t)img->width;
	return IMAGE_OK;
}




uint8_t IMAGE_OtsuThreshold(IMAGE_HandleTypeDef *img)
{
    if (img == NULL || img->pData == NULL) return 0;

    uint32_t total = (uint32_t)img->width * img->height;
    if (total == 0) return 0;

    // 1️⃣ Grayscale ise: eski davranış
    if (img->format == IMAGE_FORMAT_GRAYSCALE)
    {
        uint32_t hist[256] = {0};

        for (uint32_t i = 0; i < total; i++)
            hist[img->pData[i]]++;

        return Otsu_FromHist256(hist, total);
    }

    // 2️⃣ RGB565 ise: R,G,B için ayrı histogram
    if (img->format == IMAGE_FORMAT_RGB565)
    {
        uint32_t histR[256] = {0};
        uint32_t histG[256] = {0};
        uint32_t histB[256] = {0};

        const uint8_t *p = img->pData;

        for (uint32_t i = 0; i < total; i++)
        {
            uint16_t pix = (uint16_t)p[0] | ((uint16_t)p[1] << 8);

            uint8_t r5 = (pix >> 11) & 0x1F;
            uint8_t g6 = (pix >> 5)  & 0x3F;
            uint8_t b5 =  pix        & 0x1F;

            uint8_t r = (uint8_t)((r5 * 255u + 15u) / 31u);
            uint8_t g = (uint8_t)((g6 * 255u + 31u) / 63u);
            uint8_t b = (uint8_t)((b5 * 255u + 15u) / 31u);

            histR[r]++; histG[g]++; histB[b]++;
            p += 2;
        }

        uint8_t tR = Otsu_FromHist256(histR, total);
        uint8_t tG = Otsu_FromHist256(histG, total);
        uint8_t tB = Otsu_FromHist256(histB, total);

        // 3️⃣ Tek eşik üret
        uint16_t t = (uint16_t)(30u * tR + 59u * tG + 11u * tB);
        t = (t + 50u) / 100u;

        return (uint8_t)t;
    }

    return 0;
}


void IMAGE_ApplyThreshold(IMAGE_HandleTypeDef *img, uint8_t thresh)
{
    if (img == NULL || img->pData == NULL)
        return;

    uint32_t total = (uint32_t)img->width * img->height;

    if (img->format == IMAGE_FORMAT_GRAYSCALE)
    {
        // Gri görüntüde foreground = beyaz, background = siyah
        for (uint32_t i = 0; i < total; i++)
        {
            img->pData[i] = (img->pData[i] > thresh) ? 255 : 0;
        }
    }
    else if (img->format == IMAGE_FORMAT_RGB565)
    {
        uint8_t *p = img->pData;

        for (uint32_t i = 0; i < total; i++)
        {
            uint16_t pix = (uint16_t)p[0] | ((uint16_t)p[1] << 8);

            uint8_t r5 = (pix >> 11) & 0x1F;
            uint8_t g6 = (pix >> 5)  & 0x3F;
            uint8_t b5 =  pix        & 0x1F;

            // 8-bit’e ölçekleme
            uint8_t r = (uint8_t)((r5 * 255u + 15u) / 31u);
            uint8_t g = (uint8_t)((g6 * 255u + 31u) / 63u);
            uint8_t b = (uint8_t)((b5 * 255u + 15u) / 31u);

            // Gri hesapla
            uint8_t gray = (uint8_t)((30u * r + 59u * g + 11u * b) / 100u);

            uint16_t newPix;

            if (gray <= thresh)
            {
                // Background = siyah (0x0000)
                newPix = 0x0000;
            }
            else
            {
                // Foreground = beyaz (0xFFFF)
                newPix = 0xFFFF;
            }

            // Yaz
            p[0] = (uint8_t)(newPix & 0xFF);
            p[1] = (uint8_t)((newPix >> 8) & 0xFF);

            p += 2;
        }
    }
}


