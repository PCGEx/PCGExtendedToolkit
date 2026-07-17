// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

namespace PCGExNoise3D
{
	/**
	 * High-performance noise math utilities
	 * All functions are constexpr/FORCEINLINE for maximum optimization
	 * Thread-safe: no mutable state
	 */
	namespace Math
	{
		//
		// Constants
		//

		constexpr double F3 = 0.33333333333333333; // 1.0 / 3.0
		constexpr double G3 = 0.16666666666666666; // 1.0 / 6.0
		constexpr double F2 = 0.36602540378443864; // 0.5 * (FMath::Sqrt(3.0) - 1.0)
		constexpr double G2 = 0.21132486540518713; // (3.0 - FMath::Sqrt(3.0)) / 6.0

		// Permutation table (doubled to avoid modulo)
		inline const uint8 Perm[512] = {
			151, 160, 137, 91, 90, 15, 131, 13, 201, 95, 96, 53, 194, 233, 7, 225, 140, 36, 103, 30, 69, 142, 8, 99, 37, 240, 21, 10, 23,
			190, 6, 148, 247, 120, 234, 75, 0, 26, 197, 62, 94, 252, 219, 203, 117, 35, 11, 32, 57, 177, 33, 88, 237, 149, 56, 87, 174,
			20, 125, 136, 171, 168, 68, 175, 74, 165, 71, 134, 139, 48, 27, 166, 77, 146, 158, 231, 83, 111, 229, 122, 60, 211, 133, 230,
			220, 105, 92, 41, 55, 46, 245, 40, 244, 102, 143, 54, 65, 25, 63, 161, 1, 216, 80, 73, 209, 76, 132, 187, 208, 89, 18, 169,
			200, 196, 135, 130, 116, 188, 159, 86, 164, 100, 109, 198, 173, 186, 3, 64, 52, 217, 226, 250, 124, 123, 5, 202, 38, 147,
			118, 126, 255, 82, 85, 212, 207, 206, 59, 227, 47, 16, 58, 17, 182, 189, 28, 42, 223, 183, 170, 213, 119, 248, 152, 2, 44,
			154, 163, 70, 221, 153, 101, 155, 167, 43, 172, 9, 129, 22, 39, 253, 19, 98, 108, 110, 79, 113, 224, 232, 178, 185, 112,
			104, 218, 246, 97, 228, 251, 34, 242, 193, 238, 210, 144, 12, 191, 179, 162, 241, 81, 51, 145, 235, 249, 14, 239, 107,
			49, 192, 214, 31, 181, 199, 106, 157, 184, 84, 204, 176, 115, 121, 50, 45, 127, 4, 150, 254, 138, 236, 205, 93, 222, 114,
			67, 29, 24, 72, 243, 141, 128, 195, 78, 66, 215, 61, 156, 180,
			// Repeat
			151, 160, 137, 91, 90, 15, 131, 13, 201, 95, 96, 53, 194, 233, 7, 225, 140, 36, 103, 30, 69, 142, 8, 99, 37, 240, 21, 10, 23,
			190, 6, 148, 247, 120, 234, 75, 0, 26, 197, 62, 94, 252, 219, 203, 117, 35, 11, 32, 57, 177, 33, 88, 237, 149, 56, 87, 174,
			20, 125, 136, 171, 168, 68, 175, 74, 165, 71, 134, 139, 48, 27, 166, 77, 146, 158, 231, 83, 111, 229, 122, 60, 211, 133, 230,
			220, 105, 92, 41, 55, 46, 245, 40, 244, 102, 143, 54, 65, 25, 63, 161, 1, 216, 80, 73, 209, 76, 132, 187, 208, 89, 18, 169,
			200, 196, 135, 130, 116, 188, 159, 86, 164, 100, 109, 198, 173, 186, 3, 64, 52, 217, 226, 250, 124, 123, 5, 202, 38, 147,
			118, 126, 255, 82, 85, 212, 207, 206, 59, 227, 47, 16, 58, 17, 182, 189, 28, 42, 223, 183, 170, 213, 119, 248, 152, 2, 44,
			154, 163, 70, 221, 153, 101, 155, 167, 43, 172, 9, 129, 22, 39, 253, 19, 98, 108, 110, 79, 113, 224, 232, 178, 185, 112,
			104, 218, 246, 97, 228, 251, 34, 242, 193, 238, 210, 144, 12, 191, 179, 162, 241, 81, 51, 145, 235, 249, 14, 239, 107,
			49, 192, 214, 31, 181, 199, 106, 157, 184, 84, 204, 176, 115, 121, 50, 45, 127, 4, 150, 254, 138, 236, 205, 93, 222, 114,
			67, 29, 24, 72, 243, 141, 128, 195, 78, 66, 215, 61, 156, 180
		};

		// Gradient vectors for 3D noise (12 edges of a cube)
		inline const FVector Grad3[16] = {
			FVector(1, 1, 0), FVector(-1, 1, 0), FVector(1, -1, 0), FVector(-1, -1, 0),
			FVector(1, 0, 1), FVector(-1, 0, 1), FVector(1, 0, -1), FVector(-1, 0, -1),
			FVector(0, 1, 1), FVector(0, -1, 1), FVector(0, 1, -1), FVector(0, -1, -1),
			FVector(1, 1, 0), FVector(-1, 1, 0), FVector(0, -1, 1), FVector(0, -1, -1)
		};

		//
		// Hashing Functions
		//

		/** Fast floor for positive and negative values */
		FORCEINLINE int32 FastFloor(const double X)
		{
			const int32 Xi = static_cast<int32>(X);
			return X < Xi ? Xi - 1 : Xi;
		}

		/** Fast 3D hash */
		FORCEINLINE uint8 Hash3D(const int32 X, const int32 Y, const int32 Z)
		{
			return Perm[(Perm[(Perm[(X & 255)] + Y) & 255] + Z) & 255];
		}

		/** Seeded 3D hash */
		FORCEINLINE uint8 Hash3DSeed(const int32 X, const int32 Y, const int32 Z, const int32 Seed)
		{
			return Perm[(Perm[(Perm[((X + Seed) & 255)] + Y) & 255] + Z) & 255];
		}

		/** High-quality 32-bit hash for white noise */
		FORCEINLINE uint32 Hash32(int32 X, int32 Y, int32 Z)
		{
			// xxHash-inspired mixing
			uint32 H = static_cast<uint32>(X) * 374761393u;
			H += static_cast<uint32>(Y) * 668265263u;
			H += static_cast<uint32>(Z) * 1274126177u;
			H ^= H >> 13;
			H *= 1274126177u;
			H ^= H >> 16;
			return H;
		}

		//
		// Interpolation Functions
		//

		/** Linear interpolation */
		FORCEINLINE double Lerp(const double A, const double B, const double T)
		{
			return A + T * (B - A);
		}

		/** Quintic smoothstep (6t^5 - 15t^4 + 10t^3) - C2 continuous */
		FORCEINLINE double SmoothStep(const double T)
		{
			return T * T * T * (T * (T * 6.0 - 15.0) + 10.0);
		}

		/** Derivative of quintic smoothstep */
		FORCEINLINE double SmoothStepDeriv(const double T)
		{
			return 30.0 * T * T * (T * (T - 2.0) + 1.0);
		}

		//
		// Gradient Functions
		//

		/** Get gradient vector for 3D Perlin noise */
		FORCEINLINE FVector GetGrad3(const int32 Hash)
		{
			return Grad3[Hash & 15];
		}

		/** Dot product with gradient */
		FORCEINLINE double GradDot3(const int32 Hash, const double X, const double Y, const double Z)
		{
			const FVector& G = Grad3[Hash & 15];
			return G.X * X + G.Y * Y + G.Z * Z;
		}

		//
		// Value Noise Helpers
		//

		/** Convert hash to normalized value [0, 1] */
		FORCEINLINE double HashToDouble(const uint8 H)
		{
			return static_cast<double>(H) / 255.0;
		}

		/** Convert 32-bit hash to normalized value [0, 1] */
		FORCEINLINE double Hash32ToDouble01(const uint32 H)
		{
			return static_cast<double>(H) / 4294967295.0;
		}

		/**
		 * Derive three decorrelated [0, 1] values from a single 32-bit hash (10 bits per axis).
		 * Hash32's avalanche makes the bit lanes independent; 1024 levels is far below visual threshold.
		 */
		FORCEINLINE FVector Hash32ToVector01(const uint32 H)
		{
			return FVector(
				static_cast<double>(H & 0x3FFu) * (1.0 / 1023.0),
				static_cast<double>((H >> 10) & 0x3FFu) * (1.0 / 1023.0),
				static_cast<double>((H >> 20) & 0x3FFu) * (1.0 / 1023.0)
				);
		}

		/** Cheap secondary channel from an existing hash - decorrelates without a second full Hash32 */
		FORCEINLINE uint32 Hash32Remix(const uint32 H)
		{
			uint32 R = (H ^ 0x9E3779B9u) * 0x85EBCA6Bu;
			R ^= R >> 16;
			return R;
		}

		//
		// Cellular/Voronoi Helpers
		//

		/** Get jittered point position within a cell. Single hash, per-axis jitters from disjoint bit lanes. */
		FORCEINLINE FVector GetCellPoint(const int32 CellX, const int32 CellY, const int32 CellZ, const double Jitter, const int32 Seed)
		{
			const FVector J = Hash32ToVector01(Hash32(CellX + Seed, CellY, CellZ));

			return FVector(
				CellX + 0.5 + (J.X - 0.5) * Jitter,
				CellY + 0.5 + (J.Y - 0.5) * Jitter,
				CellZ + 0.5 + (J.Z - 0.5) * Jitter
				);
		}

		//
		// Fractal Helpers
		//

		/** Calculate total amplitude for normalization */
		FORCEINLINE double CalcFractalBounding(const int32 Octaves, const double Persistence)
		{
			double Amp = 1.0;
			double AmpSum = 0.0;
			for (int32 i = 0; i < Octaves; ++i)
			{
				AmpSum += Amp;
				Amp *= Persistence;
			}
			return 1.0 / AmpSum;
		}

		//
		// Distance Functions
		//

		FORCEINLINE double DistanceEuclidean(const FVector& A, const FVector& B)
		{
			return FVector::Dist(A, B);
		}

		FORCEINLINE double DistanceEuclideanSq(const FVector& A, const FVector& B)
		{
			return FVector::DistSquared(A, B);
		}

		FORCEINLINE double DistanceManhattan(const FVector& A, const FVector& B)
		{
			return FMath::Abs(A.X - B.X) + FMath::Abs(A.Y - B.Y) + FMath::Abs(A.Z - B.Z);
		}

		FORCEINLINE double DistanceChebyshev(const FVector& A, const FVector& B)
		{
			return FMath::Max3(FMath::Abs(A.X - B.X), FMath::Abs(A.Y - B.Y), FMath::Abs(A.Z - B.Z));
		}

		//
		// Remapping
		//

		//
		// Noise Cores
		//

		/** Classic Perlin gradient noise core, output in [-1, 1] */
		FORCEINLINE double Perlin3D(const FVector& Position, const int32 Seed)
		{
			// Find unit cube containing point
			const int32 X0 = FastFloor(Position.X);
			const int32 Y0 = FastFloor(Position.Y);
			const int32 Z0 = FastFloor(Position.Z);

			// Relative position within cube
			const double Xf = Position.X - X0;
			const double Yf = Position.Y - Y0;
			const double Zf = Position.Z - Z0;

			// Quintic interpolation curves
			const double U = SmoothStep(Xf);
			const double V = SmoothStep(Yf);
			const double W = SmoothStep(Zf);

			const int32 X0S = (X0 + Seed) & 255;

			// Hash all 8 corners
			const int32 AAA = Hash3D(X0S, Y0, Z0);
			const int32 ABA = Hash3D(X0S, Y0 + 1, Z0);
			const int32 AAB = Hash3D(X0S, Y0, Z0 + 1);
			const int32 ABB = Hash3D(X0S, Y0 + 1, Z0 + 1);
			const int32 BAA = Hash3D(X0S + 1, Y0, Z0);
			const int32 BBA = Hash3D(X0S + 1, Y0 + 1, Z0);
			const int32 BAB = Hash3D(X0S + 1, Y0, Z0 + 1);
			const int32 BBB = Hash3D(X0S + 1, Y0 + 1, Z0 + 1);

			// Gradient dot products
			const double G_AAA = GradDot3(AAA, Xf, Yf, Zf);
			const double G_BAA = GradDot3(BAA, Xf - 1.0, Yf, Zf);
			const double G_ABA = GradDot3(ABA, Xf, Yf - 1.0, Zf);
			const double G_BBA = GradDot3(BBA, Xf - 1.0, Yf - 1.0, Zf);
			const double G_AAB = GradDot3(AAB, Xf, Yf, Zf - 1.0);
			const double G_BAB = GradDot3(BAB, Xf - 1.0, Yf, Zf - 1.0);
			const double G_ABB = GradDot3(ABB, Xf, Yf - 1.0, Zf - 1.0);
			const double G_BBB = GradDot3(BBB, Xf - 1.0, Yf - 1.0, Zf - 1.0);

			// Trilinear interpolation
			const double X00 = Lerp(G_AAA, G_BAA, U);
			const double X10 = Lerp(G_ABA, G_BBA, U);
			const double X01 = Lerp(G_AAB, G_BAB, U);
			const double X11 = Lerp(G_ABB, G_BBB, U);

			const double XY0 = Lerp(X00, X10, V);
			const double XY1 = Lerp(X01, X11, V);

			return Lerp(XY0, XY1, W);
		}
	}
}
