/**************************************************************************
 * Differential geometry
 * Calculates the covariant metric tensor, and christoffel symbol terms
 * given the contravariant metric tensor terms
 **************************************************************************/

#include <bout/coordinates.hxx>
#include <utils.hxx>
#include <msg_stack.hxx>
#include <output.hxx>
#include <bout/constants.hxx>
#include <bout/assert.hxx>

#include <derivs.hxx>
#include <interpolation.hxx>
#include <fft.hxx>

#include <globals.hxx>

Coordinates::Coordinates(Mesh *mesh) {

  dx = 1.0;
  dy = 1.0;
  dz = 1.0;

  J = 1.0;
  Bxy = 1.0;

  // Identity metric tensor

  g11 = 1.0;
  g22 = 1.0;
  g33 = 1.0;
  g12 = 0.0;
  g13 = 0.0;
  g23 = 0.0;

  g_11 = 1.0;
  g_22 = 1.0;
  g_33 = 1.0;
  g_12 = 0.0;
  g_13 = 0.0;
  g_23 = 0.0;

  if (mesh->get(dx, "dx")) {
    output_warn.write("\tWARNING: differencing quantity 'dx' not found. Set to 1.0\n");
    dx = 1.0;
  }

  if (mesh->periodicX) {
    mesh->communicate(dx);
  }

  if (mesh->get(dy, "dy")) {
    output_warn.write("\tWARNING: differencing quantity 'dy' not found. Set to 1.0\n");
    dy = 1.0;
  }

  nz = mesh->LocalNz;

  if (mesh->get(dz, "dz")) {
    // Couldn't read dz from input
    int zperiod;
    BoutReal ZMIN, ZMAX;
    Options *options = Options::getRoot();
    if (options->isSet("zperiod")) {
      OPTION(options, zperiod, 1);
      ZMIN = 0.0;
      ZMAX = 1.0 / static_cast<BoutReal>(zperiod);
    } else {
      OPTION(options, ZMIN, 0.0);
      OPTION(options, ZMAX, 1.0);

      zperiod = ROUND(1.0 / (ZMAX - ZMIN));
    }

    dz = (ZMAX - ZMIN) * TWOPI / nz;
  }

  // Diagonal components of metric tensor g^{ij} (default to 1)
  mesh->get(g11, "g11", 1.0);
  mesh->get(g22, "g22", 1.0);
  mesh->get(g33, "g33", 1.0);

  // Off-diagonal elements. Default to 0
  mesh->get(g12, "g12", 0.0);
  mesh->get(g13, "g13", 0.0);
  mesh->get(g23, "g23", 0.0);

  // Check input metrics
  if ((!finite(g11)) || (!finite(g22)) || (!finite(g33))) {
    throw BoutException("\tERROR: Diagonal metrics are not finite!\n");
  }
  if ((min(g11) <= 0.0) || (min(g22) <= 0.0) || (min(g33) <= 0.0)) {
    throw BoutException("\tERROR: Diagonal metrics are negative!\n");
  }
  if ((!finite(g12)) || (!finite(g13)) || (!finite(g23))) {
    throw BoutException("\tERROR: Off-diagonal metrics are not finite!\n");
  }

  /// Find covariant metric components
  // Check if any of the components are present
  if (mesh->sourceHasVar("g_11") or mesh->sourceHasVar("g_22") or
      mesh->sourceHasVar("g_33") or mesh->sourceHasVar("g_12") or
      mesh->sourceHasVar("g_13") or mesh->sourceHasVar("g_23")) {
    // Check that all components are present
    if (mesh->sourceHasVar("g_11") and mesh->sourceHasVar("g_22") and
        mesh->sourceHasVar("g_33") and mesh->sourceHasVar("g_12") and
        mesh->sourceHasVar("g_13") and mesh->sourceHasVar("g_23")) {
      mesh->get(g_11, "g_11");
      mesh->get(g_22, "g_22");
      mesh->get(g_33, "g_33");
      mesh->get(g_12, "g_12");
      mesh->get(g_13, "g_13");
      mesh->get(g_23, "g_23");

      output_warn.write("\tWARNING! Covariant components of metric tensor set manually. "
                        "Contravariant components NOT recalculated\n");

    } else {
      output_warn.write("Not all covariant components of metric tensor found. "
                        "Calculating all from the contravariant tensor\n");
      /// Calculate contravariant metric components if not found
      if (calcCovariant()) {
        throw BoutException("Error in calcCovariant call");
      }
    }
  } else {
    /// Calculate contravariant metric components if not found
    if (calcCovariant()) {
      throw BoutException("Error in calcCovariant call");
    }
  }

  /// Calculate Jacobian and Bxy
  if (jacobian())
    throw BoutException("Error in jacobian call");

  // Attempt to read J from the grid file
  Field2D Jcalc = J;
  if (mesh->get(J, "J")) {
    output_warn.write("\tWARNING: Jacobian 'J' not found. Calculating from metric tensor\n");
    J = Jcalc;
  } else {
    // Compare calculated and loaded values
    output_warn.write("\tMaximum difference in J is %e\n", max(abs(J - Jcalc)));

    // Re-evaluate Bxy using new J
    Bxy = sqrt(g_22) / J;
  }

  // Attempt to read Bxy from the grid file
  Field2D Bcalc = Bxy;
  if (mesh->get(Bxy, "Bxy")) {
    output_warn.write("\tWARNING: Magnitude of B field 'Bxy' not found. Calculating from "
                      "metric tensor\n");
    Bxy = Bcalc;
  } else {
    output_warn.write("\tMaximum difference in Bxy is %e\n", max(abs(Bxy - Bcalc)));
    // Check Bxy
    if (!finite(Bxy)) {
      throw BoutException("\tERROR: Bxy not finite everywhere!\n");
    }
  }

  //////////////////////////////////////////////////////
  /// Calculate Christoffel symbols. Needs communication
  if (geometry()) {
    throw BoutException("Differential geometry failed\n");
  }



  if (mesh->get(ShiftTorsion, "ShiftTorsion")) {
    output_warn.write("\tWARNING: No Torsion specified for zShift. Derivatives may not be correct\n");
    ShiftTorsion = 0.0;
  }

  //////////////////////////////////////////////////////

  if (mesh->IncIntShear) {
    if (mesh->get(IntShiftTorsion, "IntShiftTorsion")) {
      output_warn.write("\tWARNING: No Integrated torsion specified\n");
      IntShiftTorsion = 0.0;
    }
  }
}

void Coordinates::outputVars(Datafile &file) {
  file.add(dx, "dx", 0);
  file.add(dy, "dy", 0);
  file.add(dz, "dz", 0);

  file.add(g11, "g11", 0);
  file.add(g22, "g22", 0);
  file.add(g33, "g33", 0);
  file.add(g12, "g12", 0);
  file.add(g13, "g13", 0);
  file.add(g23, "g23", 0);

  file.add(g_11, "g_11", 0);
  file.add(g_22, "g_22", 0);
  file.add(g_33, "g_33", 0);
  file.add(g_12, "g_12", 0);
  file.add(g_13, "g_13", 0);
  file.add(g_23, "g_23", 0);

  file.add(J, "J", 0);
}

int Coordinates::geometry() {
  TRACE("Coordinates::geometry");

  output_progress.write("Calculating differential geometry terms\n");

  if (min(abs(dx)) < 1e-8)
    throw BoutException("dx magnitude less than 1e-8");

  if (min(abs(dy)) < 1e-8)
    throw BoutException("dy magnitude less than 1e-8");

  if (fabs(dz) < 1e-8)
    throw BoutException("dz magnitude less than 1e-8");

  // Check input metrics
  if ((!finite(g11)) || (!finite(g22)) || (!finite(g33))) {
    throw BoutException("\tERROR: Diagonal metrics are not finite!\n");
  }
  if ((min(g11) <= 0.0) || (min(g22) <= 0.0) || (min(g33) <= 0.0)) {
    throw BoutException("\tERROR: Diagonal metrics are negative!\n");
  }
  if ((!finite(g12)) || (!finite(g13)) || (!finite(g23))) {
    throw BoutException("\tERROR: Off-diagonal metrics are not finite!\n");
  }

  if ((!finite(g_11)) || (!finite(g_22)) || (!finite(g_33))) {
    throw BoutException("\tERROR: Diagonal g_ij metrics are not finite!\n");
  }
  if ((min(g_11) <= 0.0) || (min(g_22) <= 0.0) || (min(g_33) <= 0.0)) {
    throw BoutException("\tERROR: Diagonal g_ij metrics are negative!\n");
  }
  if ((!finite(g_12)) || (!finite(g_13)) || (!finite(g_23))) {
    throw BoutException("\tERROR: Off-diagonal g_ij metrics are not finite!\n");
  }

  // Calculate Christoffel symbol terms (18 independent values)
  // Note: This calculation is completely general: metric
  // tensor can be 2D or 3D. For 2D, all DDZ terms are zero

  G1_11 = 0.5 * g11 * DDX(g_11) + g12 * (DDX(g_12) - 0.5 * DDY(g_11)) +
          g13 * (DDX(g_13) - 0.5 * DDZ(g_11));
  G1_22 = g11 * (DDY(g_12) - 0.5 * DDX(g_22)) + 0.5 * g12 * DDY(g_22) +
          g13 * (DDY(g_23) - 0.5 * DDZ(g_22));
  G1_33 = g11 * (DDZ(g_13) - 0.5 * DDX(g_33)) + g12 * (DDZ(g_23) - 0.5 * DDY(g_33)) +
          0.5 * g13 * DDZ(g_33);
  G1_12 = 0.5 * g11 * DDY(g_11) + 0.5 * g12 * DDX(g_22) +
          0.5 * g13 * (DDY(g_13) + DDX(g_23) - DDZ(g_12));
  G1_13 = 0.5 * g11 * DDZ(g_11) + 0.5 * g12 * (DDZ(g_12) + DDX(g_23) - DDY(g_13)) +
          0.5 * g13 * DDX(g_33);
  G1_23 = 0.5 * g11 * (DDZ(g_12) + DDY(g_13) - DDX(g_23)) +
          0.5 * g12 * (DDZ(g_22) + DDY(g_23) - DDY(g_23))
              // + 0.5 *g13*(DDZ(g_32) + DDY(g_33) - DDZ(g_23));
              // which equals
          +
          0.5 * g13 * DDY(g_33);

  G2_11 = 0.5 * g12 * DDX(g_11) + g22 * (DDX(g_12) - 0.5 * DDY(g_11)) +
          g23 * (DDX(g_13) - 0.5 * DDZ(g_11));
  G2_22 = g12 * (DDY(g_12) - 0.5 * DDX(g_22)) + 0.5 * g22 * DDY(g_22) +
          g23 * (DDY(g23) - 0.5 * DDZ(g_22));
  G2_33 = g12 * (DDZ(g_13) - 0.5 * DDX(g_33)) + g22 * (DDZ(g_23) - 0.5 * DDY(g_33)) +
          0.5 * g23 * DDZ(g_33);
  G2_12 = 0.5 * g12 * DDY(g_11) + 0.5 * g22 * DDX(g_22) +
          0.5 * g23 * (DDY(g_13) + DDX(g_23) - DDZ(g_12));
  G2_13 =
      // 0.5 *g21*(DDZ(g_11) + DDX(g_13) - DDX(g_13))
      // which equals
      0.5 * g12 * (DDZ(g_11) + DDX(g_13) - DDX(g_13))
          // + 0.5 *g22*(DDZ(g_21) + DDX(g_23) - DDY(g_13))
          // which equals
      +
      0.5 * g22 * (DDZ(g_12) + DDX(g_23) - DDY(g_13))
          // + 0.5 *g23*(DDZ(g_31) + DDX(g_33) - DDZ(g_13));
          // which equals
      +
      0.5 * g23 * DDX(g_33);
  G2_23 = 0.5 * g12 * (DDZ(g_12) + DDY(g_13) - DDX(g_23)) + 0.5 * g22 * DDZ(g_22) +
          0.5 * g23 * DDY(g_33);

  G3_11 = 0.5 * g13 * DDX(g_11) + g23 * (DDX(g_12) - 0.5 * DDY(g_11)) +
          g33 * (DDX(g_13) - 0.5 * DDZ(g_11));
  G3_22 = g13 * (DDY(g_12) - 0.5 * DDX(g_22)) + 0.5 * g23 * DDY(g_22) +
          g33 * (DDY(g_23) - 0.5 * DDZ(g_22));
  G3_33 = g13 * (DDZ(g_13) - 0.5 * DDX(g_33)) + g23 * (DDZ(g_23) - 0.5 * DDY(g_33)) +
          0.5 * g33 * DDZ(g_33);
  G3_12 =
      // 0.5 *g31*(DDY(g_11) + DDX(g_12) - DDX(g_12))
      // which equals to
      0.5 * g13 * DDY(g_11)
          // + 0.5 *g32*(DDY(g_21) + DDX(g_22) - DDY(g_12))
          // which equals to
      +
      0.5 * g23 * DDX(g_22)
          //+ 0.5 *g33*(DDY(g_31) + DDX(g_32) - DDZ(g_12));
          // which equals to
      +
      0.5 * g33 * (DDY(g_13) + DDX(g_23) - DDZ(g_12));
  G3_13 = 0.5 * g13 * DDZ(g_11) + 0.5 * g23 * (DDZ(g_12) + DDX(g_23) - DDY(g_13)) +
          0.5 * g33 * DDX(g_33);
  G3_23 = 0.5 * g13 * (DDZ(g_12) + DDY(g_13) - DDX(g_23)) + 0.5 * g23 * DDZ(g_22) +
          0.5 * g33 * DDY(g_33);

  G1 = (DDX(J * g11) + DDY(J * g12) + DDZ(J * g13)) / J;
  G2 = (DDX(J * g12) + DDY(J * g22) + DDZ(J * g23)) / J;
  G3 = (DDX(J * g13) + DDY(J * g23) + DDZ(J * g33)) / J;

  // Communicate christoffel symbol terms
  output_progress.write("\tCommunicating connection terms\n");

  FieldGroup com;

  com.add(G1_11);
  com.add(G1_22);
  com.add(G1_33);
  com.add(G1_12);
  com.add(G1_13);
  com.add(G1_23);

  com.add(G2_11);
  com.add(G2_22);
  com.add(G2_33);
  com.add(G2_12);
  com.add(G2_13);
  com.add(G2_23);

  com.add(G3_11);
  com.add(G3_22);
  com.add(G3_33);
  com.add(G3_12);
  com.add(G3_13);
  com.add(G3_23);

  com.add(G1);
  com.add(G2);
  com.add(G3);

  mesh->communicate(com);

  //////////////////////////////////////////////////////
  /// Non-uniform meshes. Need to use DDX, DDY

  OPTION(Options::getRoot(), non_uniform, false);

  Field2D d2x, d2y; // d^2 x / d i^2
  // Read correction for non-uniform meshes
  if (mesh->get(d2x, "d2x")) {
    output_warn.write("\tWARNING: differencing quantity 'd2x' not found. Calculating from dx\n");
    d1_dx = mesh->indexDDX(1. / dx); // d/di(1/dx)
  } else {
    d1_dx = -d2x / (dx * dx);
  }

  if (mesh->get(d2y, "d2y")) {
    output_warn.write("\tWARNING: differencing quantity 'd2y' not found. Calculating from dy\n");
    d1_dy = mesh->indexDDY(1. / dy); // d/di(1/dy)
  } else {
    d1_dy = -d2y / (dy * dy);
  }
  
  return 0;
}

int Coordinates::calcCovariant() {
  TRACE("Coordinates::calcCovariant");

  // Make sure metric elements are allocated
  g_11.allocate();
  g_22.allocate();
  g_33.allocate();
  g_12.allocate();
  g_13.allocate();
  g_23.allocate();

  // Perform inversion of g^{ij} to get g_{ij}
  // NOTE: Currently this bit assumes that metric terms are Field2D objects

  BoutReal **a = matrix<BoutReal>(3, 3);

  for (int jx = 0; jx < mesh->LocalNx; jx++) {
    for (int jy = 0; jy < mesh->LocalNy; jy++) {
      // set elements of g
      a[0][0] = g11(jx, jy);
      a[1][1] = g22(jx, jy);
      a[2][2] = g33(jx, jy);

      a[0][1] = a[1][0] = g12(jx, jy);
      a[1][2] = a[2][1] = g23(jx, jy);
      a[0][2] = a[2][0] = g13(jx, jy);

      // invert
      if (gaussj(a, 3)) {
        output_error.write("\tERROR: metric tensor is singular at (%d, %d)\n", jx, jy);
        return 1;
      }

      // put elements into g_{ij}
      g_11(jx, jy) = a[0][0];
      g_22(jx, jy) = a[1][1];
      g_33(jx, jy) = a[2][2];

      g_12(jx, jy) = a[0][1];
      g_13(jx, jy) = a[0][2];
      g_23(jx, jy) = a[1][2];
    }
  }

  free_matrix(a);

  BoutReal maxerr;
  maxerr= BOUTMAX(max(abs((g_11 * g11 + g_12 * g12 + g_13 * g13) - 1)),
                  max(abs((g_12 * g12 + g_22 * g22 + g_23 * g23) - 1)),
                  max(abs((g_13 * g13 + g_23 * g23 + g_33 * g33) - 1)));

  output_info.write("\tLocal maximum error in diagonal inversion is %e\n", maxerr);

  maxerr = BOUTMAX(max(abs(g_11 * g12 + g_12 * g22 + g_13 * g23)),
                   max(abs(g_11 * g13 + g_12 * g23 + g_13 * g33)),
                   max(abs(g_12 * g13 + g_22 * g23 + g_23 * g33)));

  output_info.write("\tLocal maximum error in off-diagonal inversion is %e\n", maxerr);

  return 0;
}

int Coordinates::calcContravariant() {
  TRACE("Coordinates::calcContravariant");

  // Make sure metric elements are allocated
  g11.allocate();
  g22.allocate();
  g33.allocate();
  g12.allocate();
  g13.allocate();
  g23.allocate();

  // Perform inversion of g_{ij} to get g^{ij}
  // NOTE: Currently this bit assumes that metric terms are Field2D objects

  BoutReal **a = matrix<BoutReal>(3, 3);

  for (int jx = 0; jx < mesh->LocalNx; jx++) {
    for (int jy = 0; jy < mesh->LocalNy; jy++) {
      // set elements of g
      a[0][0] = g_11(jx, jy);
      a[1][1] = g_22(jx, jy);
      a[2][2] = g_33(jx, jy);

      a[0][1] = a[1][0] = g_12(jx, jy);
      a[1][2] = a[2][1] = g_23(jx, jy);
      a[0][2] = a[2][0] = g_13(jx, jy);

      // invert
      if (gaussj(a, 3)) {
        output_error.write("\tERROR: metric tensor is singular at (%d, %d)\n", jx, jy);
        return 1;
      }

      // put elements into g_{ij}
      g11(jx, jy) = a[0][0];
      g22(jx, jy) = a[1][1];
      g33(jx, jy) = a[2][2];

      g12(jx, jy) = a[0][1];
      g13(jx, jy) = a[0][2];
      g23(jx, jy) = a[1][2];
    }
  }

  free_matrix(a);

  BoutReal maxerr;
  maxerr = BOUTMAX(max(abs((g_11 * g11 + g_12 * g12 + g_13 * g13) - 1)),
                   max(abs((g_12 * g12 + g_22 * g22 + g_23 * g23) - 1)),
                   max(abs((g_13 * g13 + g_23 * g23 + g_33 * g33) - 1)));

  output_info.write("\tMaximum error in diagonal inversion is %e\n", maxerr);

  maxerr = BOUTMAX(max(abs(g_11 * g12 + g_12 * g22 + g_13 * g23)),
                   max(abs(g_11 * g13 + g_12 * g23 + g_13 * g33)),
                   max(abs(g_12 * g13 + g_22 * g23 + g_23 * g33)));

  output_info.write("\tMaximum error in off-diagonal inversion is %e\n", maxerr);
  return 0;
}

int Coordinates::jacobian() {
  TRACE("Coordinates::jacobian");
  // calculate Jacobian using g^-1 = det[g^ij], J = sqrt(g)

  Field2D g = g11 * g22 * g33 + 2.0 * g12 * g13 * g23 - g11 * g23 * g23 -
              g22 * g13 * g13 - g33 * g12 * g12;

  // Check that g is positive
  if (min(g) < 0.0) {
    throw BoutException("The determinant of g^ij is somewhere less than 0.0");
  }
  J = 1. / sqrt(g);

  // Check jacobian
  if (!finite(J)) {
    throw BoutException("\tERROR: Jacobian not finite everywhere!\n");
  }
  if (min(abs(J)) < 1.0e-10) {
    throw BoutException("\tERROR: Jacobian becomes very small\n");
  }

  if (min(g_22) < 0.0) {
    throw BoutException("g_22 is somewhere less than 0.0");
  }
  Bxy = sqrt(g_22) / J;

  return 0;
}

/*******************************************************************************
 * Operators
 *
 *******************************************************************************/

const Field2D Coordinates::DDX(const Field2D &f) { return mesh->indexDDX(f) / dx; }

const Field2D Coordinates::DDY(const Field2D &f) { return mesh->indexDDY(f) / dy; }

const Field2D Coordinates::DDZ(const Field2D &UNUSED(f)) { return Field2D(0.0); }

#include <derivs.hxx>

/////////////////////////////////////////////////////////
// Parallel gradient

const Field2D Coordinates::Grad_par(const Field2D &var, CELL_LOC UNUSED(outloc),
                                    DIFF_METHOD UNUSED(method)) {
  TRACE("Coordinates::Grad_par( Field2D )");

  return DDY(var) / sqrt(g_22);
}

const Field3D Coordinates::Grad_par(const Field3D &var, CELL_LOC outloc,
                                    DIFF_METHOD method) {
  TRACE("Coordinates::Grad_par( Field3D )");

  return ::DDY(var, outloc, method) / sqrt(g_22);
}

/////////////////////////////////////////////////////////
// Vpar_Grad_par
// vparallel times the parallel derivative along unperturbed B-field

const Field2D Coordinates::Vpar_Grad_par(const Field2D &v, const Field2D &f,
                                         CELL_LOC UNUSED(outloc),
                                         DIFF_METHOD UNUSED(method)) {
  return VDDY(v, f) / sqrt(g_22);
}

const Field3D Coordinates::Vpar_Grad_par(const Field &v, const Field &f, CELL_LOC outloc,
                                         DIFF_METHOD method) {
  return VDDY(v, f, outloc, method) / sqrt(g_22);
}

/////////////////////////////////////////////////////////
// Parallel divergence

const Field2D Coordinates::Div_par(const Field2D &f, CELL_LOC UNUSED(outloc),
                                   DIFF_METHOD UNUSED(method)) {
  TRACE("Coordinates::Div_par( Field2D )");
  return Bxy * Grad_par(f / Bxy);
}

const Field3D Coordinates::Div_par(const Field3D &f, CELL_LOC outloc,
                                   DIFF_METHOD method) {
  TRACE("Coordinates::Div_par( Field3D )");

  if (f.hasYupYdown()) {
    // Need to modify yup and ydown fields
    Field3D f_B = f / Bxy;
    if (&f.yup() == &f) {
      // Identity, yup and ydown point to same field
      f_B.mergeYupYdown();
    } else {
      // Distinct fields
      f_B.splitYupYdown();
      f_B.yup() = f.yup() / Bxy;
      f_B.ydown() = f.ydown() / Bxy;
    }
    return Bxy * Grad_par(f_B, outloc, method);
  }

  // No yup/ydown fields. The Grad_par operator will
  // shift to field aligned coordinates
  return Bxy * Grad_par(f / Bxy, outloc, method);
}

/////////////////////////////////////////////////////////
// second parallel derivative (b dot Grad)(b dot Grad)
// Note: For parallel Laplacian use Laplace_par

const Field2D Coordinates::Grad2_par2(const Field2D &f) {
  TRACE("Coordinates::Grad2_par2( Field2D )");

  Field2D sg = sqrt(g_22);
  Field2D result = DDY(1. / sg) * DDY(f) / sg + D2DY2(f) / g_22;

  return result;
}

const Field3D Coordinates::Grad2_par2(const Field3D &f, CELL_LOC outloc) {
  TRACE("Coordinates::Grad2_par2( Field3D )");

  Field2D sg;
  Field3D result, r2;

  sg = sqrt(g_22);
  sg = DDY(1. / sg) / sg;
  if (sg.getLocation() != outloc) {
    mesh->communicate(sg);
    sg = interp_to(sg, outloc);
  }

  result = ::DDY(f, outloc);

  r2 = D2DY2(f, outloc) / interp_to(g_22, outloc);

  result = sg * result + r2;

  return result;
}

/////////////////////////////////////////////////////////
// perpendicular Laplacian operator

#include <invert_laplace.hxx> // Delp2 uses same coefficients as inversion code

const Field2D Coordinates::Delp2(const Field2D &f) {
  TRACE("Coordinates::Delp2( Field2D )");

  Field2D result = G1 * DDX(f) + g11 * D2DX2(f);

  return result;
}

const Field3D Coordinates::Delp2(const Field3D &f) {
  TRACE("Coordinates::Delp2( Field3D )");
  
  ASSERT2(mesh->xstart > 0); // Need at least one guard cell

  Field3D result;
  result.allocate();

  int ncz = mesh->LocalNz;

  static dcomplex **ft = (dcomplex **)NULL, **delft;
  if (ft == (dcomplex **)NULL) {
    // Allocate memory
    ft = matrix<dcomplex>(mesh->LocalNx, ncz / 2 + 1);
    delft = matrix<dcomplex>(mesh->LocalNx, ncz / 2 + 1);
  }

  // Loop over all y indices
  for (int jy = 0; jy < mesh->LocalNy; jy++) {

    // Take forward FFT

    for (int jx = 0; jx < mesh->LocalNx; jx++)
      rfft(&f(jx, jy, 0), ncz, ft[jx]);

    // Loop over kz
    for (int jz = 0; jz <= ncz / 2; jz++) {
      dcomplex a, b, c;

      // No smoothing in the x direction
      for (int jx = mesh->xstart; jx <= mesh->xend; jx++) {
        // Perform x derivative

        laplace_tridag_coefs(jx, jy, jz, a, b, c);

        delft[jx][jz] = a * ft[jx - 1][jz] + b * ft[jx][jz] + c * ft[jx + 1][jz];
      }
    }

    // Reverse FFT
    for (int jx = mesh->xstart; jx <= mesh->xend; jx++) {

      irfft(delft[jx], ncz, &result(jx, jy, 0));
    }

    // Boundaries
    for (int jz = 0; jz < ncz; jz++) {
      for (int jx = 0; jx<mesh->xstart; jx++) {
	result(jx, jy, jz) = 0.0;
      }
      for (int jx = mesh->xend+1; jx<mesh->LocalNx; jx++) {
	result(jx, jy, jz) = 0.0;
      }
    }
  }

  // Set the output location
  result.setLocation(f.getLocation());

  return result;
}

const FieldPerp Coordinates::Delp2(const FieldPerp &f) {
  TRACE("Coordinates::Delp2( FieldPerp )");

  FieldPerp result;
  result.allocate();

  static dcomplex **ft = (dcomplex **)NULL, **delft;

  int jy = f.getIndex();
  result.setIndex(jy);

  int ncz = mesh->LocalNz;

  if (ft == (dcomplex **)NULL) {
    // Allocate memory
    ft = matrix<dcomplex>(mesh->LocalNx, ncz / 2 + 1);
    delft = matrix<dcomplex>(mesh->LocalNx, ncz / 2 + 1);
  }

  // Take forward FFT
  for (int jx = 0; jx < mesh->LocalNx; jx++)
    rfft(f[jx], ncz, ft[jx]);

  // Loop over kz
  for (int jz = 0; jz <= ncz / 2; jz++) {

    // No smoothing in the x direction
    for (int jx = 2; jx < (mesh->LocalNx - 2); jx++) {
      // Perform x derivative

      dcomplex a, b, c;
      laplace_tridag_coefs(jx, jy, jz, a, b, c);

      delft[jx][jz] = a * ft[jx - 1][jz] + b * ft[jx][jz] + c * ft[jx + 1][jz];
    }
  }

  // Reverse FFT
  for (int jx = 1; jx < (mesh->LocalNx - 1); jx++) {
    irfft(delft[jx], ncz, result[jx]);
  }

  // Boundaries
  for (int jz = 0; jz < ncz; jz++) {
    result(0, jz) = 0.0;
    result(mesh->LocalNx - 1, jz) = 0.0;
  }

  return result;
}

const Field2D Coordinates::Laplace_par(const Field2D &f) {
  return D2DY2(f) / g_22 + DDY(J / g_22) * DDY(f) / J;
}

const Field3D Coordinates::Laplace_par(const Field3D &f) {
  return D2DY2(f) / g_22 + DDY(J / g_22) * ::DDY(f) / J;
}

// Full Laplacian operator on scalar field

const Field2D Coordinates::Laplace(const Field2D &f) {
  TRACE("Coordinates::Laplace( Field2D )");

  Field2D result =
      G1 * DDX(f) + G2 * DDY(f) + g11 * D2DX2(f) + g22 * D2DY2(f) + 2.0 * g12 * D2DXDY(f);

  return result;
}

const Field3D Coordinates::Laplace(const Field3D &f) {
  TRACE("Coordinates::Laplace( Field3D )");

  Field3D result = G1 * ::DDX(f) + G2 * ::DDY(f) + G3 * ::DDZ(f) + g11 * D2DX2(f) +
                   g22 * D2DY2(f) + g33 * D2DZ2(f) +
                   2.0 * (g12 * D2DXDY(f) + g13 * D2DXDZ(f) + g23 * D2DYDZ(f));

  return result;
}

/*******************************************************************************
 * Gauss-Jordan matrix inversion
 * used to invert metric tensor
 *******************************************************************************/

// Invert an nxn matrix using Gauss-Jordan elimination with full pivoting
int Coordinates::gaussj(BoutReal **a, int n) {
  TRACE("Coordinates::gaussj");

  int i, icol, irow, j, k, l, ll;
  BoutReal big, dum, pivinv;

  // Make sure enough temporary memory is allocated
  indxc.resize(n);
  indxr.resize(n);
  ipiv.resize(n);

  for (i = 0; i < n; i++)
    ipiv[i] = 0;

  for (i = 0; i < n; i++) { // Main loop over columns
    big = 0.0;
    irow = icol = -1;
    for (j = 0; j < n; j++) { // search for pivot element
      if (ipiv[j] != 1) {
        for (k = 0; k < n; k++) {
          if (ipiv[k] == 0) {
            if (fabs(a[j][k]) >= big) {
              big = fabs(a[j][k]);
              irow = j;
              icol = k;
            }
          } else if (ipiv[k] > 1) {
            throw BoutException("Error in GaussJ: Singular matrix-1\n");
          }
        }
      }
    }

    if (irow == -1) {
      // All elements zero!!
      throw BoutException("Error in GaussJ: Singular matrix-3\n");
    }

    ++(ipiv[icol]);
    // Now have pivot element, so interchange rows to put pivot
    // on the diagonal
    if (irow != icol) {
      for (l = 0; l < n; l++)
        swap(a[irow][l], a[icol][l]);
    }
    indxr[i] = irow;
    indxc[i] = icol;

    if (a[icol][icol] == 0.0) {
      throw BoutException("Error in GaussJ: Singular matrix-2\n");
    }
    pivinv = 1.0 / a[icol][icol];
    a[icol][icol] = 1.0;
    for (l = 0; l < n; l++)
      a[icol][l] *= pivinv;

    for (ll = 0; ll < n; ll++) { // reduce rows
      if (ll != icol) {          // except for the pivot one
        dum = a[ll][icol];
        a[ll][icol] = 0.0;
        for (l = 0; l < n; l++)
          a[ll][l] -= a[icol][l] * dum;
      }
    }
  }
  // end of main loop. Unscramble solution due to column interchanges
  for (l = n - 1; l >= 0; l--) {
    if (indxr[l] != indxc[l])
      for (k = 0; k < n; k++)
        swap(a[k][indxr[l]], a[k][indxc[l]]);
  }
  // done.

  return 0;
}
