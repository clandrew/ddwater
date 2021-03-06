#pragma once

UINT HeightMapValueToDebugRgb(float heightMap)
{
	heightMap = abs(heightMap);
	heightMap = min(heightMap, 255);
	unsigned char channel = static_cast<unsigned char>(heightMap);

	UINT rgba = 0;
	rgba |= 0xFF;
	rgba <<= 8;
	rgba |= channel;
	rgba <<= 8;
	rgba |= channel;
	rgba <<= 8;
	rgba |= channel;

	return rgba;
}

struct Color3U
{
	unsigned char R;
	unsigned char G;
	unsigned char B;

	int Difference(const Color3U& other)
	{
		int d = 0;
		d += R >= other.R ? R - other.R : other.R - R;
		d += G >= other.G ? G - other.G : other.G - G;
		d += B >= other.B ? B - other.B : other.B - B;
		return d;
	}

	void QuantizeBlue()
	{
		static const Color3U pallette[] =
		{
			{ 0x0, 0x15, 0x29 },
			{ 0x0, 0x2d, 0x5a },
			{ 0x14, 0x53, 0x92 },
			{ 0x20, 0x6d, 0xbb },
			{ 0x4e, 0xa7, 0xff },
			{ 0xaa, 0xd5, 0xff }
		};

		int bestMatchIndex = 0;
		int bestMatchDistance = Difference(pallette[0]);
		for (int i = 1; i < _countof(pallette); ++i)
		{
			int distance = Difference(pallette[i]);
			if (distance < bestMatchDistance)
			{
				bestMatchDistance = distance;
				bestMatchIndex = i;
			}
		}

		R = pallette[bestMatchIndex].R;
		G = pallette[bestMatchIndex].G;
		B = pallette[bestMatchIndex].B;
	}
};

struct Color3F
{
	float R;
	float G;
	float B;

	void Blend(Color3F const& otherColor, float otherColorAlpha)
	{
		const float invWaterAlpha = 1.0f - otherColorAlpha;
		R = (invWaterAlpha * R) + (otherColorAlpha * otherColor.R);
		G = (invWaterAlpha * G) + (otherColorAlpha * otherColor.G);
		B = (invWaterAlpha * B) + (otherColorAlpha * otherColor.B);
	}

	void Screen(Color3F const& otherColor)
	{
		R = 1 - ((1.0f - R) * (1.0f - otherColor.R));
		G = 1 - ((1.0f - G) * (1.0f - otherColor.G));
		B = 1 - ((1.0f - B) * (1.0f - otherColor.B));
	}
};

Color3U RgbUINTToColor3U(UINT rgb)
{
	Color3U result;
	result.R = (rgb >> 16) & 0xFF;
	result.G = (rgb >> 8) & 0xFF;
	result.B = (rgb >> 0) & 0xFF;
	return result;
}

Color3F Color3UToColor3F(Color3U c)
{
	Color3F result;
	result.R = static_cast<float>(c.R) / 255.0f;
	result.G = static_cast<float>(c.G) / 255.0f;
	result.B = static_cast<float>(c.B) / 255.0f;
	return result;
}

Color3U Color3FToColor3U(Color3F c)
{
	Color3U result;
	result.R = static_cast<unsigned char>(c.R * 255.0f);
	result.G = static_cast<unsigned char>(c.G * 255.0f);
	result.B = static_cast<unsigned char>(c.B * 255.0f);
	return result;
}

UINT Color3UToRgbUINT(Color3U c)
{
	UINT resultRGBA = 0;
	resultRGBA |= 0xFF;
	resultRGBA <<= 8;
	resultRGBA |= c.R;
	resultRGBA <<= 8;
	resultRGBA |= c.G;
	resultRGBA <<= 8;
	resultRGBA |= c.B;
	return resultRGBA;
}