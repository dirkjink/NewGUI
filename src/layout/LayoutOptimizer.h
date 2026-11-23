/*
 * Copyright 2007, Ingo Weinhold <bonefish@cs.tu-berlin.de>.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/layouter/LayoutOptimizer.h (commit
 * b1d497bb69).
 */
#pragma once


#include <cmath>
#include <memory>
#include <vector>

#include <ngui/SupportDefs.h>


namespace ngui::layout {


static const double kEqualsEpsilon = 0.000001;


// A matrix as Haiku's double**: row pointers into one block of values.
class Matrix {
public:
								Matrix(int m = 0, int n = 0);
								Matrix(const Matrix&) = delete;
								Matrix(Matrix&&) = default;
			Matrix&				operator=(const Matrix&) = delete;
			Matrix&				operator=(Matrix&&) = default;
									// Moving keeps the rows valid: the vector's
									// block moves with it.

								operator double**();

private:
			std::vector<double>	fValues;
			std::vector<double*> fRows;
};


class LayoutOptimizer {
public:
								LayoutOptimizer(int32 variableCount);
								~LayoutOptimizer();

			std::unique_ptr<LayoutOptimizer> Clone() const;

			bool				AddConstraint(int32 left, int32 right,
									double value, bool equality);
			bool				AddConstraintsFrom(
									const LayoutOptimizer* solver);
			void				RemoveAllConstraints();

			bool				Solve(const double* desired, double size,
									double* values);

private:
			bool				_Solve(const double* desired, double* values);
			bool				_SolveSubProblem(const double* d, int am,
									double* p);
			void				_SetResult(const double* x, double* values);

			struct Constraint;

			int32				fVariableCount;
			std::vector<std::unique_ptr<Constraint>> fConstraints;
			Matrix				fTemp1;
			Matrix				fTemp2;
			Matrix				fZtrans;
			Matrix				fQ;
			Matrix				fActiveMatrix;
			Matrix				fActiveMatrixTemp;
};


inline bool
fuzzy_equals(double a, double b)
{
	return std::fabs(a - b) < kEqualsEpsilon;
}


} // namespace ngui::layout
