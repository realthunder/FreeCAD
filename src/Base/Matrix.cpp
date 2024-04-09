/***************************************************************************
 *   Copyright (c) 2005 Imetric 3D GmbH                                    *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/


#include "PreCompiled.h"
#ifndef _PreComp_
#include <cstring>
#include <sstream>
#endif

#include "Matrix.h"
#include "Converter.h"
#include "Rotation.h"


using namespace Base;

// clang-format off
Matrix4D::Matrix4D()
    : dMtrx4D {{1., 0., 0., 0.},
               {0., 1., 0., 0.},
               {0., 0., 1., 0.},
               {0., 0., 0., 1.}}
{}

Matrix4D::Matrix4D(float a11, float a12, float a13, float a14,
                   float a21, float a22, float a23, float a24,
                   float a31, float a32, float a33, float a34,
                   float a41, float a42, float a43, float a44)
    : dMtrx4D {{a11, a12, a13, a14},
               {a21, a22, a23, a24},
               {a31, a32, a33, a34},
               {a41, a42, a43, a44}}
{}

Matrix4D::Matrix4D(double a11, double a12, double a13, double a14,
                   double a21, double a22, double a23, double a24,
                   double a31, double a32, double a33, double a34,
                   double a41, double a42, double a43, double a44)
    : dMtrx4D {{a11, a12, a13, a14},
               {a21, a22, a23, a24},
               {a31, a32, a33, a34},
               {a41, a42, a43, a44}}
{}
// clang-format on

Matrix4D::Matrix4D(const Matrix4D& mat)
    : Matrix4D()
{
    (*this) = mat;
}

Matrix4D::Matrix4D(const Vector3f& rclBase, const Vector3f& rclDir, float fAngle)
    : Matrix4D()
{
    this->rotLine(rclBase, rclDir, fAngle);
}

Matrix4D::Matrix4D(const Vector3d& rclBase, const Vector3d& rclDir, double fAngle)
    : Matrix4D()
{
    this->rotLine(rclBase, rclDir, fAngle);
}

void Matrix4D::setToUnity()
{
    dMtrx4D[0][0] = 1.0;
    dMtrx4D[0][1] = 0.0;
    dMtrx4D[0][2] = 0.0;
    dMtrx4D[0][3] = 0.0;
    dMtrx4D[1][0] = 0.0;
    dMtrx4D[1][1] = 1.0;
    dMtrx4D[1][2] = 0.0;
    dMtrx4D[1][3] = 0.0;
    dMtrx4D[2][0] = 0.0;
    dMtrx4D[2][1] = 0.0;
    dMtrx4D[2][2] = 1.0;
    dMtrx4D[2][3] = 0.0;
    dMtrx4D[3][0] = 0.0;
    dMtrx4D[3][1] = 0.0;
    dMtrx4D[3][2] = 0.0;
    dMtrx4D[3][3] = 1.0;
}

bool Matrix4D::isUnity() const
{
    return isUnity(0.0);
}

bool Matrix4D::isUnity(double tol) const
{
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (i == j) {
                if (fabs(dMtrx4D[i][j] - 1.0) > tol) {
                    return false;
                }
            }
            else {
                if (fabs(dMtrx4D[i][j]) > tol) {
                    return false;
                }
            }
        }
    }

    return true;
}

void Matrix4D::nullify()
{
    dMtrx4D[0][0] = 0.0;
    dMtrx4D[0][1] = 0.0;
    dMtrx4D[0][2] = 0.0;
    dMtrx4D[0][3] = 0.0;
    dMtrx4D[1][0] = 0.0;
    dMtrx4D[1][1] = 0.0;
    dMtrx4D[1][2] = 0.0;
    dMtrx4D[1][3] = 0.0;
    dMtrx4D[2][0] = 0.0;
    dMtrx4D[2][1] = 0.0;
    dMtrx4D[2][2] = 0.0;
    dMtrx4D[2][3] = 0.0;
    dMtrx4D[3][0] = 0.0;
    dMtrx4D[3][1] = 0.0;
    dMtrx4D[3][2] = 0.0;
    dMtrx4D[3][3] = 0.0;
}

bool Matrix4D::isNull() const
{
    // NOLINTBEGIN
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (dMtrx4D[i][j] != 0.0) {
                return false;
            }
        }
    }
    // NOLINTEND

    return true;
}

double Matrix4D::determinant() const
{
    double fA0 = dMtrx4D[0][0] * dMtrx4D[1][1] - dMtrx4D[0][1] * dMtrx4D[1][0];
    double fA1 = dMtrx4D[0][0] * dMtrx4D[1][2] - dMtrx4D[0][2] * dMtrx4D[1][0];
    double fA2 = dMtrx4D[0][0] * dMtrx4D[1][3] - dMtrx4D[0][3] * dMtrx4D[1][0];
    double fA3 = dMtrx4D[0][1] * dMtrx4D[1][2] - dMtrx4D[0][2] * dMtrx4D[1][1];
    double fA4 = dMtrx4D[0][1] * dMtrx4D[1][3] - dMtrx4D[0][3] * dMtrx4D[1][1];
    double fA5 = dMtrx4D[0][2] * dMtrx4D[1][3] - dMtrx4D[0][3] * dMtrx4D[1][2];
    double fB0 = dMtrx4D[2][0] * dMtrx4D[3][1] - dMtrx4D[2][1] * dMtrx4D[3][0];
    double fB1 = dMtrx4D[2][0] * dMtrx4D[3][2] - dMtrx4D[2][2] * dMtrx4D[3][0];
    double fB2 = dMtrx4D[2][0] * dMtrx4D[3][3] - dMtrx4D[2][3] * dMtrx4D[3][0];
    double fB3 = dMtrx4D[2][1] * dMtrx4D[3][2] - dMtrx4D[2][2] * dMtrx4D[3][1];
    double fB4 = dMtrx4D[2][1] * dMtrx4D[3][3] - dMtrx4D[2][3] * dMtrx4D[3][1];
    double fB5 = dMtrx4D[2][2] * dMtrx4D[3][3] - dMtrx4D[2][3] * dMtrx4D[3][2];
    double fDet = fA0 * fB5 - fA1 * fB4 + fA2 * fB3 + fA3 * fB2 - fA4 * fB1 + fA5 * fB0;
    return fDet;
}

double Matrix4D::determinant3() const
{
    double va = dMtrx4D[0][0] * dMtrx4D[1][1] * dMtrx4D[2][2];
    double vb = dMtrx4D[0][1] * dMtrx4D[1][2] * dMtrx4D[2][0];
    double vc = dMtrx4D[1][0] * dMtrx4D[2][1] * dMtrx4D[0][2];
    double vd = dMtrx4D[0][2] * dMtrx4D[1][1] * dMtrx4D[2][0];
    double ve = dMtrx4D[1][0] * dMtrx4D[0][1] * dMtrx4D[2][2];
    double vf = dMtrx4D[0][0] * dMtrx4D[2][1] * dMtrx4D[1][2];
    double det = (va + vb + vc) - (vd + ve + vf);
    return det;
}

void Matrix4D::move(const Vector3f& vec)
{
    move(convertTo<Vector3d>(vec));
}

void Matrix4D::move(const Vector3d& vec)
{
    dMtrx4D[0][3] += vec.x;
    dMtrx4D[1][3] += vec.y;
    dMtrx4D[2][3] += vec.z;
}

void Matrix4D::scale(const Vector3f& vec)
{
    scale(convertTo<Vector3d>(vec));
}

void Matrix4D::scale(const Vector3d& vec)
{
    Matrix4D clMat;

    clMat.dMtrx4D[0][0] = vec.x;
    clMat.dMtrx4D[1][1] = vec.y;
    clMat.dMtrx4D[2][2] = vec.z;
    (*this) = clMat * (*this);
}

void Matrix4D::rotX(double fAngle)
{
    Matrix4D clMat;
    double fsin {};
    double fcos {};

    fsin = sin(fAngle);
    fcos = cos(fAngle);
    clMat.dMtrx4D[1][1] = fcos;
    clMat.dMtrx4D[2][2] = fcos;
    clMat.dMtrx4D[1][2] = -fsin;
    clMat.dMtrx4D[2][1] = fsin;

    (*this) = clMat * (*this);
}

void Matrix4D::rotY(double fAngle)
{
    Matrix4D clMat;
    double fsin {};
    double fcos {};

    fsin = sin(fAngle);
    fcos = cos(fAngle);
    clMat.dMtrx4D[0][0] = fcos;
    clMat.dMtrx4D[2][2] = fcos;
    clMat.dMtrx4D[2][0] = -fsin;
    clMat.dMtrx4D[0][2] = fsin;

    (*this) = clMat * (*this);
}

void Matrix4D::rotZ(double fAngle)
{
    Matrix4D clMat;
    double fsin {};
    double fcos {};

    fsin = sin(fAngle);
    fcos = cos(fAngle);
    clMat.dMtrx4D[0][0] = fcos;
    clMat.dMtrx4D[1][1] = fcos;
    clMat.dMtrx4D[0][1] = -fsin;
    clMat.dMtrx4D[1][0] = fsin;

    (*this) = clMat * (*this);
}

void Matrix4D::rotLine(const Vector3d& vec, double fAngle)
{
    // **** algorithm was taken from a math book
    Matrix4D clMA;
    Matrix4D clMB;
    Matrix4D clMC;
    Matrix4D clMRot;
    Vector3d clRotAxis(vec);
    double fcos {};
    double fsin {};

    // set all entries to "0"
    for (short iz = 0; iz < 4; iz++) {
        for (short is = 0; is < 4; is++) {
            clMA.dMtrx4D[iz][is] = 0;
            clMB.dMtrx4D[iz][is] = 0;
            clMC.dMtrx4D[iz][is] = 0;
        }
    }

    // ** normalize the rotation axis
    clRotAxis.Normalize();

    // ** set the rotation matrix (formula taken from a math book) */
    fcos = cos(fAngle);
    fsin = sin(fAngle);

    clMA.dMtrx4D[0][0] = (1 - fcos) * clRotAxis.x * clRotAxis.x;
    clMA.dMtrx4D[0][1] = (1 - fcos) * clRotAxis.x * clRotAxis.y;
    clMA.dMtrx4D[0][2] = (1 - fcos) * clRotAxis.x * clRotAxis.z;
    clMA.dMtrx4D[1][0] = (1 - fcos) * clRotAxis.x * clRotAxis.y;
    clMA.dMtrx4D[1][1] = (1 - fcos) * clRotAxis.y * clRotAxis.y;
    clMA.dMtrx4D[1][2] = (1 - fcos) * clRotAxis.y * clRotAxis.z;
    clMA.dMtrx4D[2][0] = (1 - fcos) * clRotAxis.x * clRotAxis.z;
    clMA.dMtrx4D[2][1] = (1 - fcos) * clRotAxis.y * clRotAxis.z;
    clMA.dMtrx4D[2][2] = (1 - fcos) * clRotAxis.z * clRotAxis.z;

    clMB.dMtrx4D[0][0] = fcos;
    clMB.dMtrx4D[1][1] = fcos;
    clMB.dMtrx4D[2][2] = fcos;

    clMC.dMtrx4D[0][1] = -fsin * clRotAxis.z;
    clMC.dMtrx4D[0][2] = fsin * clRotAxis.y;
    clMC.dMtrx4D[1][0] = fsin * clRotAxis.z;
    clMC.dMtrx4D[1][2] = -fsin * clRotAxis.x;
    clMC.dMtrx4D[2][0] = -fsin * clRotAxis.y;
    clMC.dMtrx4D[2][1] = fsin * clRotAxis.x;

    for (short iz = 0; iz < 3; iz++) {
        for (short is = 0; is < 3; is++) {
            clMRot.dMtrx4D[iz][is] =
                clMA.dMtrx4D[iz][is] + clMB.dMtrx4D[iz][is] + clMC.dMtrx4D[iz][is];
        }
    }

    (*this) = clMRot * (*this);
}

void Matrix4D::rotLine(const Vector3f& vec, float fAngle)
{
    Vector3d tmp = convertTo<Vector3d>(vec);
    rotLine(tmp, static_cast<double>(fAngle));
}

void Matrix4D::rotLine(const Vector3d& rclBase, const Vector3d& rclDir, double fAngle)
{
    Matrix4D clMRot;
    clMRot.rotLine(rclDir, fAngle);
    transform(rclBase, clMRot);
}

void Matrix4D::rotLine(const Vector3f& rclBase, const Vector3f& rclDir, float fAngle)
{
    Vector3d pnt = convertTo<Vector3d>(rclBase);
    Vector3d dir = convertTo<Vector3d>(rclDir);
    rotLine(pnt, dir, static_cast<double>(fAngle));
}

/**
 * If this matrix describes a rotation around an arbitrary axis with a translation (in axis
 * direction) then the base point of the axis, its direction, the rotation angle and the translation
 * part get calculated. In this case the return value is set to true, if this matrix doesn't
 * describe a rotation false is returned.
 *
 * The translation vector can be calculated with \a fTranslation * \a rclDir, whereas the length of
 * \a rclDir is normalized to 1.
 *
 * Note: In case the \a fTranslation part is zero then passing \a rclBase, \a rclDir and \a rfAngle
 * to a new matrix object creates an identical matrix.
 */
bool Matrix4D::toAxisAngle(Vector3f& rclBase,
                           Vector3f& rclDir,
                           float& rfAngle,
                           float& fTranslation) const
{
    Vector3d pnt = convertTo<Vector3d>(rclBase);
    Vector3d dir = convertTo<Vector3d>(rclDir);
    double dAngle = static_cast<double>(rfAngle);
    double dTranslation = static_cast<double>(fTranslation);

    bool ok = toAxisAngle(pnt, dir, dAngle, dTranslation);
    if (ok) {
        rclBase = convertTo<Vector3f>(pnt);
        rclDir = convertTo<Vector3f>(dir);
        rfAngle = static_cast<float>(dAngle);
        fTranslation = static_cast<float>(dTranslation);
    }

    return ok;
}

bool Matrix4D::toAxisAngle(Vector3d& rclBase,
                           Vector3d& rclDir,
                           double& rfAngle,
                           double& fTranslation) const
{
    // First check if the 3x3 submatrix is orthogonal
    for (int i = 0; i < 3; i++) {
        // length must be one
        if (fabs(dMtrx4D[0][i] * dMtrx4D[0][i] + dMtrx4D[1][i] * dMtrx4D[1][i]
                 + dMtrx4D[2][i] * dMtrx4D[2][i] - 1.0)
            > 0.01) {
            return false;
        }
        // scalar product with other rows must be zero
        if (fabs(dMtrx4D[0][i] * dMtrx4D[0][(i + 1) % 3] + dMtrx4D[1][i] * dMtrx4D[1][(i + 1) % 3]
                 + dMtrx4D[2][i] * dMtrx4D[2][(i + 1) % 3])
            > 0.01) {
            return false;
        }
    }

    // Okay, the 3x3 matrix is orthogonal.
    // Note: The section to get the rotation axis and angle was taken from WildMagic Library.
    //
    // Let (x,y,z) be the unit-length axis and let A be an angle of rotation.
    // The rotation matrix is R = I + sin(A)*P + (1-cos(A))*P^2 where
    // I is the identity and
    //
    //       +-        -+
    //   P = |  0 -z +y |
    //       | +z  0 -x |
    //       | -y +x  0 |
    //       +-        -+
    //
    // If A > 0, R represents a counterclockwise rotation about the axis in
    // the sense of looking from the tip of the axis vector towards the
    // origin.  Some algebra will show that
    //
    //   cos(A) = (trace(R)-1)/2  and  R - R^t = 2*sin(A)*P
    //
    // In the event that A = pi, R-R^t = 0 which prevents us from extracting
    // the axis through P.  Instead note that R = I+2*P^2 when A = pi, so
    // P^2 = (R-I)/2.  The diagonal entries of P^2 are x^2-1, y^2-1, and
    // z^2-1.  We can solve these for axis (x,y,z).  Because the angle is pi,
    // it does not matter which sign you choose on the square roots.
    //
    // For more details see also http://www.math.niu.edu/~rusin/known-math/97/rotations

    double fTrace = dMtrx4D[0][0] + dMtrx4D[1][1] + dMtrx4D[2][2];
    double fCos = 0.5 * (fTrace - 1.0);
    rfAngle = acos(fCos);  // in [0,PI]

    if (rfAngle > 0.0) {
        if (rfAngle < D_PI) {
            rclDir.x = (dMtrx4D[2][1] - dMtrx4D[1][2]);
            rclDir.y = (dMtrx4D[0][2] - dMtrx4D[2][0]);
            rclDir.z = (dMtrx4D[1][0] - dMtrx4D[0][1]);
            rclDir.Normalize();
        }
        else {
            // angle is PI
            double fHalfInverse {};
            if (dMtrx4D[0][0] >= dMtrx4D[1][1]) {
                // r00 >= r11
                if (dMtrx4D[0][0] >= dMtrx4D[2][2]) {
                    // r00 is maximum diagonal term
                    rclDir.x = (0.5 * sqrt(dMtrx4D[0][0] - dMtrx4D[1][1] - dMtrx4D[2][2] + 1.0));
                    fHalfInverse = 0.5 / rclDir.x;
                    rclDir.y = (fHalfInverse * dMtrx4D[0][1]);
                    rclDir.z = (fHalfInverse * dMtrx4D[0][2]);
                }
                else {
                    // r22 is maximum diagonal term
                    rclDir.z = (0.5 * sqrt(dMtrx4D[2][2] - dMtrx4D[0][0] - dMtrx4D[1][1] + 1.0));
                    fHalfInverse = 0.5 / rclDir.z;
                    rclDir.x = (fHalfInverse * dMtrx4D[0][2]);
                    rclDir.y = (fHalfInverse * dMtrx4D[1][2]);
                }
            }
            else {
                // r11 > r00
                if (dMtrx4D[1][1] >= dMtrx4D[2][2]) {
                    // r11 is maximum diagonal term
                    rclDir.y = (0.5 * sqrt(dMtrx4D[1][1] - dMtrx4D[0][0] - dMtrx4D[2][2] + 1.0));
                    fHalfInverse = 0.5 / rclDir.y;
                    rclDir.x = (fHalfInverse * dMtrx4D[0][1]);
                    rclDir.z = (fHalfInverse * dMtrx4D[1][2]);
                }
                else {
                    // r22 is maximum diagonal term
                    rclDir.z = (0.5 * sqrt(dMtrx4D[2][2] - dMtrx4D[0][0] - dMtrx4D[1][1] + 1.0));
                    fHalfInverse = 0.5 / rclDir.z;
                    rclDir.x = (fHalfInverse * dMtrx4D[0][2]);
                    rclDir.y = (fHalfInverse * dMtrx4D[1][2]);
                }
            }
        }
    }
    else {
        // The angle is 0 and the matrix is the identity.  Any axis will
        // work, so just use the x-axis.
        rclDir.x = 1.0;
        rclDir.y = 0.0;
        rclDir.z = 0.0;
        rclBase.x = 0.0;
        rclBase.y = 0.0;
        rclBase.z = 0.0;
    }

    // This is the translation part in axis direction
    fTranslation = (dMtrx4D[0][3] * rclDir.x + dMtrx4D[1][3] * rclDir.y + dMtrx4D[2][3] * rclDir.z);
    Vector3d cPnt(dMtrx4D[0][3], dMtrx4D[1][3], dMtrx4D[2][3]);
    cPnt = cPnt - fTranslation * rclDir;

    // This is the base point of the rotation axis
    if (rfAngle > 0.0) {
        double factor = 0.5 * (1.0 + fTrace) / sin(rfAngle);
        rclBase.x = (0.5 * (cPnt.x + factor * (rclDir.y * cPnt.z - rclDir.z * cPnt.y)));
        rclBase.y = (0.5 * (cPnt.y + factor * (rclDir.z * cPnt.x - rclDir.x * cPnt.z)));
        rclBase.z = (0.5 * (cPnt.z + factor * (rclDir.x * cPnt.y - rclDir.y * cPnt.x)));
    }

    return true;
}

void Matrix4D::transform(const Vector3f& vec, const Matrix4D& mat)
{
    move(-vec);
    (*this) = mat * (*this);
    move(vec);
}

void Matrix4D::transform(const Vector3d& vec, const Matrix4D& mat)
{
    move(-vec);
    (*this) = mat * (*this);
    move(vec);
}

void Matrix4D::inverse()
{
    Matrix4D clInvTrlMat;
    Matrix4D clInvRotMat;

    /**** Herausnehmen und Inversion der TranslationsMatrix
    aus der TransformationMatrix                      ****/
    for (short iz = 0; iz < 3; iz++) {
        clInvTrlMat.dMtrx4D[iz][3] = -dMtrx4D[iz][3];
    }

    /**** Herausnehmen und Inversion der RotationsMatrix
    aus der TransformationMatrix                      ****/
    for (short iz = 0; iz < 3; iz++) {
        for (short is = 0; is < 3; is++) {
            clInvRotMat.dMtrx4D[iz][is] = dMtrx4D[is][iz];
        }
    }

    /**** inv(M) = inv(Mtrl * Mrot) = inv(Mrot) * inv(Mtrl) ****/
    (*this) = clInvRotMat * clInvTrlMat;
}

using Matrix = double*;

// NOLINTBEGIN
void Matrix_gauss(Matrix a, Matrix b)
{
    int ipiv[4], indxr[4], indxc[4];
    int i {}, j {}, k {}, l {}, ll {};
    int irow = 0, icol = 0;
    double big {}, pivinv {};
    double dum {};
    for (j = 0; j < 4; j++) {
        ipiv[j] = 0;
    }
    for (i = 0; i < 4; i++) {
        big = 0;
        for (j = 0; j < 4; j++) {
            if (ipiv[j] != 1) {
                for (k = 0; k < 4; k++) {
                    if (ipiv[k] == 0) {
                        if (fabs(a[4 * j + k]) >= big) {
                            big = fabs(a[4 * j + k]);
                            irow = j;
                            icol = k;
                        }
                    }
                    else if (ipiv[k] > 1) {
                        return; /* Singular matrix */
                    }
                }
            }
        }
        ipiv[icol] = ipiv[icol] + 1;
        if (irow != icol) {
            for (l = 0; l < 4; l++) {
                dum = a[4 * irow + l];
                a[4 * irow + l] = a[4 * icol + l];
                a[4 * icol + l] = dum;
            }
            for (l = 0; l < 4; l++) {
                dum = b[4 * irow + l];
                b[4 * irow + l] = b[4 * icol + l];
                b[4 * icol + l] = dum;
            }
        }
        indxr[i] = irow;
        indxc[i] = icol;
        if (a[4 * icol + icol] == 0.0) {
            return;
        }
        pivinv = 1.0 / a[4 * icol + icol];
        a[4 * icol + icol] = 1.0;
        for (l = 0; l < 4; l++) {
            a[4 * icol + l] = a[4 * icol + l] * pivinv;
        }
        for (l = 0; l < 4; l++) {
            b[4 * icol + l] = b[4 * icol + l] * pivinv;
        }
        for (ll = 0; ll < 4; ll++) {
            if (ll != icol) {
                dum = a[4 * ll + icol];
                a[4 * ll + icol] = 0;
                for (l = 0; l < 4; l++) {
                    a[4 * ll + l] = a[4 * ll + l] - a[4 * icol + l] * dum;
                }
                for (l = 0; l < 4; l++) {
                    b[4 * ll + l] = b[4 * ll + l] - b[4 * icol + l] * dum;
                }
            }
        }
    }
    for (l = 3; l >= 0; l--) {
        if (indxr[l] != indxc[l]) {
            for (k = 0; k < 4; k++) {
                dum = a[4 * k + indxr[l]];
                a[4 * k + indxr[l]] = a[4 * k + indxc[l]];
                a[4 * k + indxc[l]] = dum;
            }
        }
    }
}
// NOLINTEND

void Matrix4D::inverseOrthogonal()
{
    Base::Vector3d vec(dMtrx4D[0][3], dMtrx4D[1][3], dMtrx4D[2][3]);
    transpose();
    vec = this->operator*(vec);
    dMtrx4D[0][3] = -vec.x;
    dMtrx4D[3][0] = 0;
    dMtrx4D[1][3] = -vec.y;
    dMtrx4D[3][1] = 0;
    dMtrx4D[2][3] = -vec.z;
    dMtrx4D[3][2] = 0;
}

void Matrix4D::inverseGauss()
{
    double matrix[16];
    // clang-format off
    double inversematrix[16] = {1, 0, 0, 0,
                                0, 1, 0, 0,
                                0, 0, 1, 0,
                                0, 0, 0, 1};
    // clang-format on
    getGLMatrix(matrix);

    Matrix_gauss(matrix, inversematrix);

    setGLMatrix(inversematrix);
}

void Matrix4D::getMatrix(double dMtrx[16]) const
{
    for (short iz = 0; iz < 4; iz++) {
        for (short is = 0; is < 4; is++) {
            dMtrx[4 * iz + is] = dMtrx4D[iz][is];
        }
    }
}

void Matrix4D::setMatrix(const double dMtrx[16])
{
    for (short iz = 0; iz < 4; iz++) {
        for (short is = 0; is < 4; is++) {
            dMtrx4D[iz][is] = dMtrx[4 * iz + is];
        }
    }
}

void Matrix4D::getGLMatrix(double dMtrx[16]) const
{
    for (short iz = 0; iz < 4; iz++) {
        for (short is = 0; is < 4; is++) {
            dMtrx[iz + 4 * is] = dMtrx4D[iz][is];
        }
    }
}

void Matrix4D::setGLMatrix(const double dMtrx[16])
{
    for (short iz = 0; iz < 4; iz++) {
        for (short is = 0; is < 4; is++) {
            dMtrx4D[iz][is] = dMtrx[iz + 4 * is];
        }
    }
}

unsigned long Matrix4D::getMemSpace()
{
    return sizeof(Matrix4D);
}

void Matrix4D::Print() const
{
    // NOLINTBEGIN
    for (short i = 0; i < 4; i++) {
        printf("%9.3f %9.3f %9.3f %9.3f\n",
               dMtrx4D[i][0],
               dMtrx4D[i][1],
               dMtrx4D[i][2],
               dMtrx4D[i][3]);
    }
    // NOLINTEND
}

void Matrix4D::transpose()
{
    double dNew[4][4];

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            dNew[j][i] = dMtrx4D[i][j];
        }
    }

    memcpy(dMtrx4D, dNew, sizeof(dMtrx4D));
}


// write the 12 double of the matrix in a stream
std::string Matrix4D::toString() const
{
    std::stringstream str;
    // NOLINTBEGIN
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            str << dMtrx4D[i][j] << " ";
        }
    }
    // NOLINTEND

    return str.str();
}

// read the 12 double of the matrix from a stream
void Matrix4D::fromString(const std::string& str)
{
    std::stringstream input;
    input.str(str);

    // NOLINTBEGIN
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            input >> dMtrx4D[i][j];
        }
    }
    // NOLINTEND
}

// Analyse the a transformation Matrix and describe the transformation
std::string Matrix4D::analyse() const
{
    const double eps = 1.0e-06;
    bool hastranslation = (dMtrx4D[0][3] != 0.0 || dMtrx4D[1][3] != 0.0 || dMtrx4D[2][3] != 0.0);
    const Base::Matrix4D unityMatrix = Base::Matrix4D();
    std::string text;
    if (*this == unityMatrix) {
        text = "Unity Matrix";
    }
    else {
        if (dMtrx4D[3][0] != 0.0 || dMtrx4D[3][1] != 0.0 || dMtrx4D[3][2] != 0.0
            || dMtrx4D[3][3] != 1.0) {
            text = "Projection";
        }
        else  // translation and affine
        {
            if (dMtrx4D[0][1] == 0.0 && dMtrx4D[0][2] == 0.0 && dMtrx4D[1][0] == 0.0
                && dMtrx4D[1][2] == 0.0 && dMtrx4D[2][0] == 0.0 && dMtrx4D[2][1] == 0.0)  // scaling
            {
                std::ostringstream stringStream;
                stringStream << "Scale [" << dMtrx4D[0][0] << ", " << dMtrx4D[1][1] << ", "
                             << dMtrx4D[2][2] << "]";
                text = stringStream.str();
            }
            else {
                Base::Matrix4D sub;
                sub[0][0] = dMtrx4D[0][0];
                sub[0][1] = dMtrx4D[0][1];
                sub[0][2] = dMtrx4D[0][2];
                sub[1][0] = dMtrx4D[1][0];
                sub[1][1] = dMtrx4D[1][1];
                sub[1][2] = dMtrx4D[1][2];
                sub[2][0] = dMtrx4D[2][0];
                sub[2][1] = dMtrx4D[2][1];
                sub[2][2] = dMtrx4D[2][2];

                Base::Matrix4D trp = sub;
                trp.transpose();
                trp = trp * sub;
                bool ortho = true;
                for (unsigned short i = 0; i < 4 && ortho; i++) {
                    for (unsigned short j = 0; j < 4 && ortho; j++) {
                        if (i != j) {
                            if (fabs(trp[i][j]) > eps) {
                                ortho = false;
                                break;
                            }
                        }
                    }
                }

                double determinant = sub.determinant();
                if (ortho) {
                    if (fabs(determinant - 1.0) < eps)  // rotation
                    {
                        text = "Rotation Matrix";
                    }
                    else {
                        if (fabs(determinant + 1.0) < eps)  // rotation
                        {
                            text = "Rotinversion Matrix";
                        }
                        else  // scaling with rotation
                        {
                            std::ostringstream stringStream;
                            stringStream << "Scale and Rotate ";
                            if (determinant < 0.0) {
                                stringStream << "and Invert ";
                            }
                            stringStream << "[ " << sqrt(trp[0][0]) << ", " << sqrt(trp[1][1])
                                         << ", " << sqrt(trp[2][2]) << "]";
                            text = stringStream.str();
                        }
                    }
                }
                else {
                    std::ostringstream stringStream;
                    stringStream << "Affine with det= " << determinant;
                    text = stringStream.str();
                }
            }
        }
        if (hastranslation) {
            text += " with Translation";
        }
    }
    return text;
}

Matrix4D& Matrix4D::Outer(const Vector3f& rV1, const Vector3f& rV2)
{
    setToUnity();

    Outer(convertTo<Vector3d>(rV1), convertTo<Vector3d>(rV2));

    return *this;
}

Matrix4D& Matrix4D::Outer(const Vector3d& rV1, const Vector3d& rV2)
{
    setToUnity();

    dMtrx4D[0][0] = rV1.x * rV2.x;
    dMtrx4D[0][1] = rV1.x * rV2.y;
    dMtrx4D[0][2] = rV1.x * rV2.z;

    dMtrx4D[1][0] = rV1.y * rV2.x;
    dMtrx4D[1][1] = rV1.y * rV2.y;
    dMtrx4D[1][2] = rV1.y * rV2.z;

    dMtrx4D[2][0] = rV1.z * rV2.x;
    dMtrx4D[2][1] = rV1.z * rV2.y;
    dMtrx4D[2][2] = rV1.z * rV2.z;

    return *this;
}

Matrix4D& Matrix4D::Hat(const Vector3f& rV)
{
    setToUnity();

    Hat(convertTo<Vector3d>(rV));

    return *this;
}

Matrix4D& Matrix4D::Hat(const Vector3d& rV)
{
    setToUnity();

    dMtrx4D[0][0] = 0.0;
    dMtrx4D[0][1] = -rV.z;
    dMtrx4D[0][2] = rV.y;

    dMtrx4D[1][0] = rV.z;
    dMtrx4D[1][1] = 0.0;
    dMtrx4D[1][2] = -rV.x;

    dMtrx4D[2][0] = -rV.y;
    dMtrx4D[2][1] = rV.x;
    dMtrx4D[2][2] = 0.0;

    return *this;
}

ScaleType Matrix4D::hasScale(double tol) const
{
    const double defaultTolerance = 1e-9;
    // check for uniform scaling
    //
    // For a scaled rotation matrix it matters whether
    // the scaling was applied from the left or right side.
    // Only in case of uniform scaling it doesn't make a difference.
    if (tol == 0.0) {
        tol = defaultTolerance;
    }

    // check if the absolute values are proportionally close or equal
    auto closeAbs = [&](double val_a, double val_b) {
        double abs_a = fabs(val_a);
        double abs_b = fabs(val_b);
        if (abs_b > abs_a) {
            return (abs_b - abs_a) / abs_b <= tol;
        }
        if (abs_a > abs_b) {
            return (abs_a - abs_b) / abs_a <= tol;
        }
        return true;
    };

    // get column vectors
    double dx = getCol(0).Sqr();
    double dy = getCol(1).Sqr();
    double dz = getCol(2).Sqr();
    double dxyz = sqrt(dx * dy * dz);

    // get row vectors
    double du = getRow(0).Sqr();
    double dv = getRow(1).Sqr();
    double dw = getRow(2).Sqr();
    double duvw = sqrt(du * dv * dw);

    double d3 = determinant3();

    // This could be e.g. a projection, a shearing,... matrix
    if (!closeAbs(dxyz, d3) && !closeAbs(duvw, d3)) {
        return ScaleType::Other;
    }

    if (closeAbs(duvw, d3) && (!closeAbs(du, dv) || !closeAbs(dv, dw))) {
        return ScaleType::NonUniformLeft;
    }

    if (closeAbs(dxyz, d3) && (!closeAbs(dx, dy) || !closeAbs(dy, dz))) {
        return ScaleType::NonUniformRight;
    }

    if (fabs(d3 - 1.0) > tol) {
        return ScaleType::Uniform;
    }

    return ScaleType::NoScaling;
}

////////////////////////////////////////////////////////////////////////////////
// Following code modified from Coin3D Matrix.cpp for decompose matrix into
// translation, rotation and scale.
////////////////////////////////////////////////////////////////////////////////

/*
 * declarations for polar_decomp algorithm from Graphics Gems IV,
 * by Ken Shoemake <shoemake@graphics.cis.upenn.edu>
 */
enum QuatPart {X, Y, Z, W};
typedef double HMatrix[4][4]; /* Right-handed, for column vectors */
struct Vector4d{
  double v[4];

  Vector4d() {}

  Vector4d(double x, double y, double z, double w)
      :v{x,y,z,w}
  {}

  double operator[](int idx) const
  {
      return v[idx];
  }
  double &operator[](int idx)
  {
      return v[idx];
  }
};
typedef struct {
  Vector4d t;    /* Translation components */
  Rotation  q;        /* Essential rotation     */
  Rotation  u;        /* Stretch rotation       */
  Vector4d k;    /* Stretch factors        */
  double f;      /* Sign of determinant    */
} AffineParts;
static double polar_decomp(const HMatrix &M, HMatrix Q, HMatrix S);
static Vector4d spect_decomp(HMatrix S, HMatrix U);
static Rotation snuggle(Rotation q, Vector4d & k);
static void decomp_affine(const HMatrix &A, AffineParts * parts);

/***********************************************************************
   below is the polar_decomp implementation by Ken Shoemake
   <shoemake@graphics.cis.upenn.edu>. It was part of the
   Graphics Gems IV archive.
************************************************************************/

/**** Decompose.c ****/
/* Ken Shoemake, 1993 */

/******* Matrix Preliminaries *******/

/** Fill out 3x3 matrix to 4x4 **/
#define mat_pad(A) (A[W][X]=A[X][W]=A[W][Y]=A[Y][W]=A[W][Z]=A[Z][W]=0, A[W][W]=1)

/** Copy nxn matrix A to C using "gets" for assignment **/
#define mat_copy(C, gets, A, n) {int i, j; for (i=0;i<n;i++) for (j=0;j<n;j++)\
    C[i][j] gets (A[i][j]);}

/** Copy transpose of nxn matrix A to C using "gets" for assignment **/
#define mat_tpose(AT, gets, A, n) {int i, j; for (i=0;i<n;i++) for (j=0;j<n;j++)\
    AT[i][j] gets (A[j][i]);}

/** Assign nxn matrix C the element-wise combination of A and B using "op" **/
#define mat_binop(C, gets, A, op, B, n) {int i, j; for (i=0;i<n;i++) for (j=0;j<n;j++)\
    C[i][j] gets (A[i][j]) op (B[i][j]);}

/** Multiply the upper left 3x3 parts of A and B to get AB **/
static void
mat_mult(HMatrix A, const HMatrix &B, HMatrix AB)
{
  int i, j;
  for (i=0; i<3; i++) for (j=0; j<3; j++)
    AB[i][j] = A[i][0]*B[0][j] + A[i][1]*B[1][j] + A[i][2]*B[2][j];
}

/** Return dot product of length 3 vectors va and vb **/
static double
vdot(double * va, double * vb)
{
  return (va[0]*vb[0] + va[1]*vb[1] + va[2]*vb[2]);
}

/** Set v to cross product of length 3 vectors va and vb **/
static void
vcross(double * va, double * vb, double * v)
{
  v[0] = va[1]*vb[2] - va[2]*vb[1];
  v[1] = va[2]*vb[0] - va[0]*vb[2];
  v[2] = va[0]*vb[1] - va[1]*vb[0];
}

/** Set MadjT to transpose of inverse of M times determinant of M **/
static void
adjoint_transpose(HMatrix M, HMatrix MadjT)
{
  vcross(M[1], M[2], MadjT[0]);
  vcross(M[2], M[0], MadjT[1]);
  vcross(M[0], M[1], MadjT[2]);
}

/******* Decomp Auxiliaries *******/

static HMatrix mat_id = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};

/** Compute either the 1 or infinity norm of M, depending on tpose **/
static double
mat_norm(HMatrix M, int tpose)
{
  int i;
  double sum, max;
  max = 0.0;
  for (i=0; i<3; i++) {
    if (tpose) sum = static_cast<double>(fabs(M[0][i])+fabs(M[1][i])+fabs(M[2][i]));
    else sum = static_cast<double>(fabs(M[i][0])+fabs(M[i][1])+fabs(M[i][2]));
    if (max<sum) max = sum;
  }
  return max;
}

static double norm_inf(HMatrix M) {return mat_norm(M, 0);}
static double norm_one(HMatrix M) {return mat_norm(M, 1);}

/** Return index of column of M containing maximum abs entry, or -1 if M=0 **/
static int
find_max_col(HMatrix M)
{
  double abs, max;
  int i, j, col;
  max = 0.0; col = -1;
  for (i=0; i<3; i++) for (j=0; j<3; j++) {
    abs = M[i][j]; if (abs<0.0) abs = -abs;
    if (abs>max) {max = abs; col = j;}
  }
    return col;
}

/** Setup u for Household reflection to zero all v components but first **/
static void
make_reflector(double * v, double * u)
{
  double s = static_cast<double>(sqrt(vdot(v, v)));
  u[0] = v[0]; u[1] = v[1];
  u[2] = v[2] + ((v[2]<0.0) ? -s : s);
  s = static_cast<double>(sqrt(2.0/vdot(u, u)));
  u[0] = u[0]*s; u[1] = u[1]*s; u[2] = u[2]*s;
}

/** Apply Householder reflection represented by u to column vectors of M **/
static void
reflect_cols(HMatrix M, double * u)
{
  int i, j;
  for (i=0; i<3; i++) {
    double s = u[0]*M[0][i] + u[1]*M[1][i] + u[2]*M[2][i];
    for (j=0; j<3; j++) M[j][i] -= u[j]*s;
  }
}
/** Apply Householder reflection represented by u to row vectors of M **/
static void
reflect_rows(HMatrix M, double * u)
{
  int i, j;
  for (i=0; i<3; i++) {
    double s = vdot(u, M[i]);
    for (j=0; j<3; j++) M[i][j] -= u[j]*s;
  }
}

/** Find orthogonal factor Q of rank 1 (or less) M **/
static void
do_rank1(HMatrix M, HMatrix Q)
{
  double v1[3], v2[3], s;
  int col;
  mat_copy(Q, =, mat_id, 4);
  /* If rank(M) is 1, we should find a non-zero column in M */
  col = find_max_col(M);
  if (col<0) return; /* Rank is 0 */
  v1[0] = M[0][col]; v1[1] = M[1][col]; v1[2] = M[2][col];
  make_reflector(v1, v1); reflect_cols(M, v1);
  v2[0] = M[2][0]; v2[1] = M[2][1]; v2[2] = M[2][2];
  make_reflector(v2, v2); reflect_rows(M, v2);
  s = M[2][2];
  if (s<0.0) Q[2][2] = -1.0;
  reflect_cols(Q, v1); reflect_rows(Q, v2);
}

/** Find orthogonal factor Q of rank 2 (or less) M using adjoint transpose **/
static void
do_rank2(HMatrix M, HMatrix MadjT, HMatrix Q)
{
  double v1[3], v2[3];
  double w, x, y, z, c, s, d;
  int col;
  /* If rank(M) is 2, we should find a non-zero column in MadjT */
  col = find_max_col(MadjT);
  if (col<0) {do_rank1(M, Q); return;} /* Rank<2 */
  v1[0] = MadjT[0][col]; v1[1] = MadjT[1][col]; v1[2] = MadjT[2][col];
  make_reflector(v1, v1); reflect_cols(M, v1);
  vcross(M[0], M[1], v2);
  make_reflector(v2, v2); reflect_rows(M, v2);
  w = M[0][0]; x = M[0][1]; y = M[1][0]; z = M[1][1];
  if (w*z>x*y) {
    c = z+w; s = y-x; d = static_cast<double>(sqrt(c*c+s*s)); c = c/d; s = s/d;
    Q[0][0] = Q[1][1] = c; Q[0][1] = -(Q[1][0] = s);
  }
  else {
    c = z-w; s = y+x; d = static_cast<double>(sqrt(c*c+s*s)); c = c/d; s = s/d;
    Q[0][0] = -(Q[1][1] = c); Q[0][1] = Q[1][0] = s;
  }
  Q[0][2] = Q[2][0] = Q[1][2] = Q[2][1] = 0.0; Q[2][2] = 1.0;
  reflect_cols(Q, v1); reflect_rows(Q, v2);
}



/******* Polar Decomposition *******/

/* Polar Decomposition of 3x3 matrix in 4x4,
 * M = QS.  See Nicholas Higham and Robert S. Schreiber,
 * Fast Polar Decomposition of An Arbitrary Matrix,
 * Technical Report 88-942, October 1988,
 * Department of Computer Science, Cornell University.
 */
static double
polar_decomp(const HMatrix &M, HMatrix Q, HMatrix S)
{
#define TOL 1.0e-6
  HMatrix Mk, MadjTk, Ek;
  double det, M_one, M_inf, MadjT_one, MadjT_inf, E_one, gamma, g1, g2;
  int i, j;
  mat_tpose(Mk, =, M, 3);
  M_one = norm_one(Mk);  M_inf = norm_inf(Mk);
  do {
    adjoint_transpose(Mk, MadjTk);
    det = vdot(Mk[0], MadjTk[0]);
    if (det==0.0) {do_rank2(Mk, MadjTk, Mk); break;}
    MadjT_one = norm_one(MadjTk); MadjT_inf = norm_inf(MadjTk);
    gamma = static_cast<double>(sqrt(sqrt((MadjT_one*MadjT_inf)/(M_one*M_inf))/fabs(det)));
    g1 = gamma*0.5;
    g2 = 0.5/(gamma*det);
    mat_copy(Ek, =, Mk, 3);
    mat_binop(Mk, =, g1*Mk, +, g2*MadjTk, 3);
    mat_copy(Ek, -=, Mk, 3);
    E_one = norm_one(Ek);
    M_one = norm_one(Mk);  M_inf = norm_inf(Mk);
  } while (E_one>(M_one*TOL));
  mat_tpose(Q, =, Mk, 3); mat_pad(Q);
  mat_mult(Mk, M, S);    mat_pad(S);
  for (i=0; i<3; i++) for (j=i; j<3; j++)
    S[i][j] = S[j][i] = 0.5*(S[i][j]+S[j][i]);
  return (det);
}

/******* Spectral Decomposition *******/

/* Compute the spectral decomposition of symmetric positive semi-definite S.
 * Returns rotation in U and scale factors in result, so that if K is a diagonal
 * matrix of the scale factors, then S = U K (U transpose). Uses Jacobi method.
 * See Gene H. Golub and Charles F. Van Loan. Matrix Computations. Hopkins 1983.
 */
static Vector4d
spect_decomp(HMatrix S, HMatrix U)
{
  Vector4d kv;
  double Diag[3], OffD[3]; /* OffD is off-diag (by omitted index) */
  double g, h, fabsh, fabsOffDi, t, theta, c, s, tau, ta, OffDq, a, b;
  static char nxt[] = {Y, Z, X};
  int sweep, i, j;
  mat_copy(U, =, mat_id, 4);
  Diag[X] = S[X][X]; Diag[Y] = S[Y][Y]; Diag[Z] = S[Z][Z];
  OffD[X] = S[Y][Z]; OffD[Y] = S[Z][X]; OffD[Z] = S[X][Y];
  for (sweep=20; sweep>0; sweep--) {
    double sm = static_cast<double>(fabs(OffD[X])+fabs(OffD[Y])+fabs(OffD[Z]));
    if (sm==0.0) break;
    for (i=Z; i>=X; i--) {
      int p = nxt[i]; int q = nxt[p];
      fabsOffDi = fabs(OffD[i]);
      g = 100.0*fabsOffDi;
      if (fabsOffDi>0.0) {
        h = Diag[q] - Diag[p];
        fabsh = fabs(h);
        if (fabsh+g==fabsh) {
          t = OffD[i]/h;
        }
        else {
          theta = 0.5*h/OffD[i];
          t = 1.0/(fabs(theta)+sqrt(theta*theta+1.0));
          if (theta<0.0) t = -t;
        }
        c = 1.0/sqrt(t*t+1.0); s = t*c;
        tau = s/(c+1.0);
        ta = t*OffD[i]; OffD[i] = 0.0;
        Diag[p] -= ta; Diag[q] += ta;
        OffDq = OffD[q];
        OffD[q] -= s*(OffD[p] + tau*OffD[q]);
        OffD[p] += s*(OffDq   - tau*OffD[p]);
        for (j=Z; j>=X; j--) {
          a = U[j][p]; b = U[j][q];
          U[j][p] -= static_cast<double>(s*(b + tau*a));
          U[j][q] += static_cast<double>(s*(a - tau*b));
        }
      }
    }
  }
  kv[X] = static_cast<double>(Diag[X]);
  kv[Y] = static_cast<double>(Diag[Y]);
  kv[Z] = static_cast<double>(Diag[Z]);
  kv[W] = 1.0;
  return (kv);
}

/* Helper function for the snuggle() function below. */
static inline void
cycle(double * a, bool flip)
{
  if (flip) {
    a[3]=a[0]; a[0]=a[1]; a[1]=a[2]; a[2]=a[3];
  }
  else {
    a[3]=a[2]; a[2]=a[1]; a[1]=a[0]; a[0]=a[3];
  }
}

/******* Spectral Axis Adjustment *******/

/* Given a unit quaternion, q, and a scale vector, k, find a unit quaternion, p,
 * which permutes the axes and turns freely in the plane of duplicate scale
 * factors, such that q p has the largest possible w component, i.e. the
 * smallest possible angle. Permutes k's components to go with q p instead of q.
 * See Ken Shoemake and Tom Duff. Matrix Animation and Polar Decomposition.
 * Proceedings of Graphics Interface 1992. Details on p. 262-263.
 */
static Rotation
snuggle(Rotation q, Vector4d & k)
{
#define SQRTHALF (0.7071067811865475244f)
#define sgn(n, v)    ((n)?-(v):(v))
#define swap(a, i, j) {a[3]=a[i]; a[i]=a[j]; a[j]=a[3];}

  Rotation p;
  double ka[4];
  int i, turn = -1;
  ka[X] = k[X]; ka[Y] = k[Y]; ka[Z] = k[Z];
  if (ka[X]==ka[Y]) {if (ka[X]==ka[Z]) turn = W; else turn = Z;}
  else {if (ka[X]==ka[Z]) turn = Y; else if (ka[Y]==ka[Z]) turn = X;}
  if (turn>=0) {
    Rotation qtoz, qp;
    unsigned neg[3], win;
    double mag[3], t;
    static Rotation qxtoz(0.0, SQRTHALF, 0.0, SQRTHALF);
    static Rotation qytoz(SQRTHALF, 0.0, 0.0, SQRTHALF);
    static Rotation qppmm(0.5, 0.5, -0.5, -0.5);
    static Rotation qpppp(0.5, 0.5, 0.5, 0.5);
    static Rotation qmpmm(-0.5, 0.5, -0.5, -0.5);
    static Rotation qpppm(0.5, 0.5, 0.5, -0.5);
    static Rotation q0001(0.0, 0.0, 0.0, 1.0);
    static Rotation q1000(1.0, 0.0, 0.0, 0.0);
    switch (turn) {
    default: return Rotation(q).invert();
    case X: q = (qtoz = qxtoz) * q; swap(ka, X, Z) break;
    case Y: q = (qtoz = qytoz) * q; swap(ka, Y, Z) break;
    case Z: qtoz = q0001; break;
    }
    q.invert();
    mag[0] = static_cast<double>(q.getValue()[Z]*q.getValue()[Z])+static_cast<double>(q.getValue()[W]*q.getValue()[W]-0.5);
    mag[1] = static_cast<double>(q.getValue()[X]*q.getValue()[Z])-static_cast<double>(q.getValue()[Y]*q.getValue()[W]);
    mag[2] = static_cast<double>(q.getValue()[Y]*q.getValue()[Z]+static_cast<double>(q.getValue()[X]*q.getValue()[W]));
    for (i=0; i<3; i++) if ((neg[i] = (mag[i] < 0.0))) mag[i] = -mag[i];
    if (mag[0]>mag[1]) {if (mag[0]>mag[2]) win = 0; else win = 2;}
    else {if (mag[1]>mag[2]) win = 1; else win = 2;}
    switch (win) {
    case 0: if (neg[0]) p = q1000; else p = q0001; break;
    case 1: if (neg[1]) p = qppmm; else p = qpppp; cycle(ka, false); break;
    case 2: if (neg[2]) p = qmpmm; else p = qpppm; cycle(ka, true); break;
    }
    qp = p * q;
    t = sqrt(mag[win]+0.5);
    p = Rotation(0.0, 0.0, -qp.getValue()[Z]/static_cast<double>(t), qp.getValue()[W]/static_cast<double>(t)) * p;
    p = Rotation(p).invert() * qtoz;
  }
  else {
    double qa[4], pa[4];
    unsigned lo, hi, neg[4], par = 0;
    double all, big, two;
    qa[0] = q.getValue()[X]; qa[1] = q.getValue()[Y]; qa[2] = q.getValue()[Z]; qa[3] = q.getValue()[W];
    for (i=0; i<4; i++) {
      pa[i] = 0.0;
      if ((neg[i] = (qa[i]<0.0))) qa[i] = -qa[i];
      par ^= neg[i];
    }
    /* Find two largest components, indices in hi and lo */
    if (qa[0]>qa[1]) lo = 0; else lo = 1;
    if (qa[2]>qa[3]) hi = 2; else hi = 3;
    if (qa[lo]>qa[hi]) {
      if (qa[lo^1]>qa[hi]) {hi = lo; lo ^= 1;}
      else {hi ^= lo; lo ^= hi; hi ^= lo;}
    } else {if (qa[hi^1]>qa[lo]) lo = hi^1;}
    all = (qa[0]+qa[1]+qa[2]+qa[3])*0.5;
    two = (qa[hi]+qa[lo])*SQRTHALF;
    big = qa[hi];
    if (all>two) {
      if (all>big) {/*all*/
        {int i; for (i=0; i<4; i++) pa[i] = sgn(neg[i], 0.5);}
        cycle(ka, par);
      }
      else {/*big*/ pa[hi] = sgn(neg[hi], 1.0);}
    }
    else {
      if (two>big) {/*two*/
        pa[hi] = sgn(neg[hi], SQRTHALF); pa[lo] = sgn(neg[lo], SQRTHALF);
        if (lo>hi) {hi ^= lo; lo ^= hi; hi ^= lo;}
        if (hi==W) {hi = "\001\002\000"[lo]; lo = 3-hi-lo;}
        swap(ka, hi, lo)
          } else {/*big*/ pa[hi] = sgn(neg[hi], 1.0);}
    }
    // FIXME: p = conjugate(pa)? 20010114 mortene.
    p.setValue(-pa[0], -pa[1], -pa[2], pa[3]);
  }
  k[X] = ka[X]; k[Y] = ka[Y]; k[Z] = ka[Z];
  return (p);
}

/******* Decompose Affine Matrix *******/

/* Decompose 4x4 affine matrix A as TFRUK(U transpose), where t contains the
 * translation components, q contains the rotation R, u contains U, k contains
 * scale factors, and f contains the sign of the determinant.
 * Assumes A transforms column vectors in right-handed coordinates.
 * See Ken Shoemake and Tom Duff. Matrix Animation and Polar Decomposition.
 * Proceedings of Graphics Interface 1992.
 */
static void
decomp_affine(const HMatrix &A, AffineParts * parts)
{
  HMatrix Q, S, U;
  Rotation p;
  parts->t = Vector4d(A[X][W], A[Y][W], A[Z][W], 0);
  double det = polar_decomp(A, Q, S);
  if (det<0.0) {
    mat_copy(Q, =, -Q, 3);
    parts->f = -1;
  }
  else parts->f = 1;

  // reinterpret_cast to humor MSVC6, this should have no effect on
  // other compilers.
  Matrix4D TQ;
  TQ.setMatrix(reinterpret_cast<const double *>(&Q));
  // parts->q = Rotation(TQ.transpose());
  parts->q = Rotation(TQ);
  parts->k = spect_decomp(S, U);
  // Transpose for our code (we use OpenGL's convention for numbering
  // rows and columns).

  // reinterpret_cast to humor MSVC6, this should have no effect on
  // other compilers.
  Matrix4D TU;
  TU.setMatrix(reinterpret_cast<const double *>(&U));
  // parts->u = Rotation(TU.transpose());
  parts->u = Rotation(TU);
  p = snuggle(parts->u, parts->k);
  parts->u = p * parts->u;
}

void Matrix4D::getTransform(Vector3d & t, Rotation & r,
                            Vector3d & s, Rotation & so) const
{
  AffineParts parts;

  // transpose-copy
  decomp_affine(this->dMtrx4D, &parts);

  double mul = 1.0;
  if (parts.t[W] != 0.0) mul = 1.0 / parts.t[W];
  t[0] = parts.t[X] * mul;
  t[1] = parts.t[Y] * mul;
  t[2] = parts.t[Z] * mul;

  r = parts.q;
  mul = 1.0;
  if (parts.k[W] != 0.0) mul = 1.0 / parts.k[W];
  // mul be sign of determinant to support negative scales.
  mul *= parts.f;
  s[0] = parts.k[X] * mul;
  s[1] = parts.k[Y] * mul;
  s[2] = parts.k[Z] * mul;

  so = parts.u;
}

void
Matrix4D::getTransform(Vector3d & translation,
                       Rotation & rotation,
                       Vector3d & scaleFactor,
                       Rotation & scaleOrientation,
                       const Vector3d & center) const
{
    Matrix4D tmp;
    tmp.move(center);
    tmp *= *this;
    tmp.move(Vector3d() - center);
    tmp.getTransform(translation, rotation, scaleFactor, scaleOrientation);
}

void Matrix4D::setTransform(const Vector3d & t, const Rotation & r, const Vector3d & s)
{
    this->setToUnity();
    this->scale(s);
    Matrix4D tmp;
    r.getValue(tmp);
    *this *= tmp;
    this->move(t);
}

void
Matrix4D::setTransform(const Vector3d & t, const Rotation & r,
                       const Vector3d & s, const Rotation & so)
{
    Matrix4D tmp;
    so.inverse().getValue(tmp);
    *this = tmp;
    this->scale(s);
    so.getValue(tmp);
    *this *= tmp;
    r.getValue(tmp);
    *this *= tmp;
    this->move(t);
}

void
Matrix4D::setTransform(const Vector3d & translation,
                       const Rotation & rotation,
                       const Vector3d & scaleFactor,
                       const Rotation & scaleOrientation,
                       const Vector3d & center)
{
    this->setToUnity();
    this->move(Vector3d() - center);
    Matrix4D tmp;
    scaleOrientation.inverse().getValue(tmp);
    *this *= tmp;
    this->scale(scaleFactor);
    scaleOrientation.getValue(tmp);
    *this *= tmp;
    rotation.getValue(tmp);
    *this *= tmp;
    this->move(translation);
    this->move(center);
}

std::array<Matrix4D, 4> Matrix4D::decompose() const
{
    // decompose the matrix to shear, scale, rotation and move
    // so that matrix = move * rotation * scale * shear
    // return an array of matrices
    Matrix4D moveMatrix;
    Matrix4D rotationMatrix;
    Matrix4D scaleMatrix;
    Matrix4D residualMatrix(*this);
    // extract transform
    moveMatrix.move(residualMatrix.getCol(3));
    residualMatrix.setCol(3, Vector3d());
    // find and extract rotation
    int prim_dir = -1;
    std::array<Vector3d, 3> dirs = {Vector3d(1., 0., 0.),
                                    Vector3d(0., 1., 0.),
                                    Vector3d(0., 0., 1.)};
    for (int i = 0; i < 3; i++) {
        if (residualMatrix.getCol(i).IsNull()) {
            continue;
        }
        if (prim_dir < 0) {
            dirs[i] = residualMatrix.getCol(i);
            dirs[i].Normalize();
            prim_dir = i;
            continue;
        }

        Vector3d cross = dirs[prim_dir].Cross(residualMatrix.getCol(i));
        if (cross.IsNull()) {
            continue;
        }
        cross.Normalize();
        int last_dir = 3 - i - prim_dir;
        if (i - prim_dir == 1) {
            dirs[last_dir] = cross;
            dirs[i] = cross.Cross(dirs[prim_dir]);
        }
        else {
            dirs[last_dir] = -cross;
            dirs[i] = dirs[prim_dir].Cross(-cross);
        }
        prim_dir = -2;  // done
        break;
    }
    if (prim_dir >= 0) {
        // handle case with only one valid direction
        Vector3d cross = dirs[prim_dir].Cross(Vector3d(0., 0., 1.));
        if (cross.IsNull()) {
            cross = dirs[prim_dir].Cross(Vector3d(0., 1., 0.));
        }
        dirs[(prim_dir + 1) % 3] = cross;
        dirs[(prim_dir + 2) % 3] = dirs[prim_dir].Cross(cross);
    }
    rotationMatrix.setCol(0, dirs[0]);
    rotationMatrix.setCol(1, dirs[1]);
    rotationMatrix.setCol(2, dirs[2]);
    rotationMatrix.inverseGauss();
    residualMatrix = rotationMatrix * residualMatrix;
    // To keep signs of the scale factors equal
    if (residualMatrix.determinant() < 0) {
        rotationMatrix.rotZ(D_PI);
        residualMatrix.rotZ(D_PI);
    }
    rotationMatrix.inverseGauss();
    // extract scale
    double xScale = residualMatrix.dMtrx4D[0][0];
    double yScale = residualMatrix.dMtrx4D[1][1];
    double zScale = residualMatrix.dMtrx4D[2][2];
    scaleMatrix.dMtrx4D[0][0] = xScale;
    scaleMatrix.dMtrx4D[1][1] = yScale;
    scaleMatrix.dMtrx4D[2][2] = zScale;
    // The remaining shear
    residualMatrix.scale(xScale != 0 ? 1.0 / xScale : 1.0,
                         yScale != 0 ? 1.0 / yScale : 1.0,
                         zScale != 0 ? 1.0 / zScale : 1.0);
    // Restore trace in shear matrix
    residualMatrix.setDiagonal(Vector3d(1.0, 1.0, 1.0));
    // Remove values close to zero
    for (int i = 0; i < 3; i++) {
        if (std::abs(scaleMatrix.dMtrx4D[i][i]) < 1e-15) {
            scaleMatrix.dMtrx4D[i][i] = 0.0;
        }
        for (int j = 0; j < 3; j++) {
            if (std::abs(residualMatrix.dMtrx4D[i][j]) < 1e-15) {
                residualMatrix.dMtrx4D[i][j] = 0.0;
            }
            if (std::abs(rotationMatrix.dMtrx4D[i][j]) < 1e-15) {
                rotationMatrix.dMtrx4D[i][j] = 0.0;
            }
        }
    }
    return std::array<Matrix4D, 4> {residualMatrix, scaleMatrix, rotationMatrix, moveMatrix};
}
