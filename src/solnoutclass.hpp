/**
* File: solnoutclass.h
* Author: Michael R. Hayashi
* Date: 6 November 2018
* Desc: Header for solution and output custom classes
*/

// Make sure this file is included only once
#ifndef solnoutclass_h
#define solnoutclass_h

#define _USE_MATH_DEFINES // Place before including <cmath> for e, log2(e), log10(e), ln(2), ln(10), pi, pi/2, pi/4, 1/pi, 2/pi, 2/sqrt(pi), sqrt(2), and 1/sqrt(2)
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <cctype>
#include <cmath>
#include <complex>
#include <ctime>
#include <algorithm>
#include <taskflow/taskflow.hpp>
#include <parser-spef/parser-spef.hpp>
#include <Eigen/Core>
#include <Eigen/LU>
#include <Eigen/Sparse>
#include "limboint.hpp"
#include "fdtd.hpp"

// Parasitics representation macros
//#define EIGEN_SPARSE // Enable to use sparse matrices for parasitics reporting
//#define EIGEN_COMPRESS // Enable to allow compressed sparse row (CSR) format for sparse matrices
#define WRITE_THRESH (1.e-7) // Threshold for saving a parasitic value to file as fraction of total represented in matrix

// Define types for Eigen
typedef Eigen::Triplet<double, int> dTriplet;
typedef Eigen::Triplet<complex<double>, int> cdTriplet;
typedef Eigen::SparseMatrix<double, Eigen::RowMajor> spMat; // Real-valued sparse matrix defined always
typedef Eigen::SparseMatrix<complex<double>, Eigen::RowMajor> cspMat; // Complex-valued sparse matrix defined always
#ifdef EIGEN_SPARSE
typedef spMat dMat;
typedef cspMat cdMat;
#else
typedef Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> dMat;
typedef Eigen::Matrix<complex<double>, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> cdMat;
#endif
struct myPruneFunctor
{
  private:
    double reference;

  public:
    // Default constructor
    myPruneFunctor()
    {
        this->reference = 0.;
    }

    // Parametrizer constructor
    myPruneFunctor(double ref)
    {
        this->reference = ref;
    }

    // Functor magic overloading parentheses
    inline bool operator()(const int &row, const int &col, const double &value) const
    {
        return (abs(value) > this->reference);
    }
};

// Custom class for apertures in Gerber files (RS-274X specification)
class Aperture
{
  public:
    int aperNum;      // D-code aperture number
    char stanTemp;    // Standard aperture template (aperture macros disallowed)
    double circumDia; // Circumdiameter (m, twice radius of circumcircle)
    double xSize;     // Maximum extent in x-direction (m)
    double ySize;     // Maximum extent in y-direction (m)
    double holeDia;   // Diameter of center hole (m)
    int numVert;      // Number of vertices (for regular polygon template only)
    double rotation;  // Rotation angle (rad, for regular polygon template)
  public:
    // Default constructor
    Aperture()
    {
        this->aperNum = 0;
        this->stanTemp = 'C'; // Assume circle by default
        this->circumDia = 0.0;
        this->xSize = 0.0; 
        this->ySize = 0.0;
        this->holeDia = 0.0;
        this->numVert = 0;
        this->rotation = 0.0;
    }

    // Parametrized constructor (circles only)
    Aperture(int aperNum, double circumDia, double holeDia)
    {
        this->aperNum = aperNum;
        this->stanTemp = 'C'; // Only option is circle
        this->circumDia = circumDia;
        this->xSize = circumDia; // Circle presents diameter as maximum extent in x-direction
        this->ySize = circumDia; // Circle presents diameter as maximum extent in y-direction
        this->holeDia = holeDia;
        this->numVert = 0;
        this->rotation = 0.0; // Perfect circle cannot have an overall rotation
    }

    // Parametrized constructor (rectangles and obrounds/stadia)
    Aperture(int aperNum, char stanTemp, double xSize, double ySize, double holeDia)
    {
        this->aperNum = aperNum;
        if ((stanTemp != 'C') && (stanTemp != 'R') && (stanTemp != 'O') && (stanTemp != 'P'))
        {
            cerr << "Aperture standard templates must be 'C' (circle), 'R' (rectangle), 'O' (obround), or 'P' (polygon). Defaulting to 'R' for this constructor." << endl;
            this->stanTemp = 'R';
            this->circumDia = hypot(xSize, ySize); // Circumdiameter of a rectangle is the diagonal length
            this->numVert = 4;
            this->rotation = atan2(-ySize, xSize); // Effective rotation of diagonal to the horizontal (xSize > ySize) or vertical (ySize > xSize) orientation
        }
        else if ((stanTemp == 'C') && (this->xSize == this->ySize))
        {
            cerr << "This constructor is not meant for circles. Accepting input regardless." << endl;
            this->stanTemp = 'C';
            this->circumDia = xSize;
            this->numVert = 0;
            this->rotation = 0.0;
        }
        else if ((stanTemp == 'C') && (this->xSize != this->ySize))
        {
            cerr << "This constructor is not meant for circles. A circular aperture must have ySize equal to xSize. Defaulting to obround of given dimensions." << endl;
            this->stanTemp = 'O';
            this->circumDia = (xSize > ySize) ? xSize : ySize; // Ternary operator to select the larger dimension as the circumdiameter
            this->numVert = 0;
            this->rotation = (xSize > ySize) ? atan2(-ySize, xSize - ySize) : atan2(-(ySize - xSize), xSize); // Lower-right point of rectangle within stadium has rotation found by removing semicircle
        }
        else if (stanTemp == 'P')
        {
            cerr << "This constructor is not meant for regular polygons. Defaulting to standard rectangle of same dimensions." << endl;
            this->stanTemp = 'R';
            this->circumDia = hypot(xSize, ySize);
            this->numVert = 4;
            this->rotation = atan2(-ySize, xSize);
        }
        else if (stanTemp == 'R')
        {
            this->stanTemp = 'R';
            this->circumDia = hypot(xSize, ySize);
            this->numVert = 4;
            this->rotation = atan2(-ySize, xSize); // Effective rotation of diagonal to the horizontal (xSize > ySize) or vertical (ySize > xSize) orientation
        }
        else
        {
            this->stanTemp = 'O'; // Obround is only option left
            this->circumDia = (xSize > ySize) ? xSize : ySize; // Ternary operator to select the larger dimension as the circumdiameter
            this->numVert = 0;
            this->rotation = (xSize > ySize) ? atan2(-ySize, xSize - ySize) : atan2(-(ySize - xSize), xSize); // Lower-right point of rectangle within stadium has rotation found by removing semicircle
        }
        this->xSize = xSize;
        this->ySize = ySize;
        this->holeDia = holeDia;
    }

    // Parametrized constructor (regular polygons only)
    Aperture(int aperNum, char stanTemp, double circumDia, double holeDia, int numVert, double rotation)
    {
        this->aperNum = aperNum;
        if ((stanTemp != 'P') && (stanTemp != 'R'))
        {
            cerr << "This constructor only supports regular polygons. Treating input as regular polygon anyway." << endl;
        }
        else if ((stanTemp == 'R') && (numVert != 4))
        {
            cerr << "This constructor only supports regular polygons, and rectangles would have 4 vertices. Treating input as regular polygon." << endl;
        }
        else if ((stanTemp == 'R') && (numVert == 4))
        {
            cerr << "This constructor only supports regular polygons, not rectangles. Interpreting input as rotated square regardless." << endl;
        }
        this->stanTemp = 'P';
        this->circumDia = circumDia;
        this->xSize = circumDia; // This is not the true extent, but the calculation is too complicated for the value here
        this->ySize = circumDia; // Same inaccuracy as xSize mentioned above
        this->holeDia = holeDia;
        this->numVert = numVert;
        this->rotation = rotation;
    }

    // Get D-code aperture number
    int getAperNum() const
    {
        return this->aperNum;
    }

    // Get the standard aperture template used (aperture macros disallowed)
    // ('C' = circle, 'R' = rectangle, 'O' = obround/stadium, 'P' = regular polygon)
    char getStanTemp() const
    {
        return this->stanTemp;
    }

    // Get the circumdiamter of the aperture (m)
    double getCircumDia() const
    {
        return this->circumDia;
    }

    // Get the maximum aperture extent in the x-direction (m)
    double getXSize() const
    {
        return this->xSize;
    }

    // Get the maximum aperture extent in the y-direction (m)
    double getYSize() const
    {
        return this->ySize;
    }

    // Get the diameter of the center hole (m)
    double getHoleDia() const
    {
        return this->holeDia;
    }

    // Get the number of vertices of the regular polygon standard aperture
    // (3 = triangle, 4 = square, ..., 12 = dodecagon)
    int getNumVert() const
    {
        return this->numVert;
    }

    // Get the rotation of the regular polgyon standard aperture (rad)
    // (zero rotation has one vertex on positive x-axis)
    double getRotation() const
    {
        return this->rotation;
    }

    // Is the aperture a circle?
    bool isCircle() const
    {
        if (this->stanTemp == 'C')
        {
            return true;
        }
        else if ((this->stanTemp == 'O') && (this->xSize == this->ySize))
        {
            return true;
        }
        else
        {
            return false;
        }
    }

    // Is the aperture a square?
    bool isSquare() const
    {
        if ((this->stanTemp == 'R') && (this->xSize == this->ySize))
        {
            return true;
        }
        else if ((this->stanTemp == 'P') && (this->numVert == 4))
        {
            return true;
        }
        else
        {
            return false;
        }
    }

    // Draw aperture as GDSII boundary at given position
    boundary drawAsBound(double xo, double yo)
    {
        // Find coordinates of boundary based on standard aperture template
        vector<double> bounds;
        if (this->isCircle())
        {
            // Approximate circle as 24-gon with ending point same as starting point at phi=0
            for (size_t indi = 0; indi <= 24; indi++)
            {
                bounds.push_back(xo + 0.5 * this->circumDia * cos(2.0 * M_PI * indi / 24)); // x-coordinate
                bounds.push_back(yo + 0.5 * this->circumDia * sin(2.0 * M_PI * indi / 24)); // y-coordinate
            }
        }
        else if (this->stanTemp == 'R')
        {
            // Push back pairs of coordinates for the 5 rectangle points CCW starting from lower-right with first point repeated
            bounds.push_back(xo + 0.5 * this->xSize); // Lower-right
            bounds.push_back(yo - 0.5 * this->ySize);
            bounds.push_back(xo + 0.5 * this->xSize); // Upper-right
            bounds.push_back(yo + 0.5 * this->ySize);
            bounds.push_back(xo - 0.5 * this->xSize); // Upper-left
            bounds.push_back(yo + 0.5 * this->ySize);
            bounds.push_back(xo - 0.5 * this->xSize); // Lower-left
            bounds.push_back(yo - 0.5 * this->ySize);
            bounds.push_back(xo + 0.5 * this->xSize); // Lower-right repeated
            bounds.push_back(yo - 0.5 * this->ySize);
        }
        else if (this->stanTemp == 'O')
        {
            // Approximate stadium as rectangle between two semi-24-gons (instead of semicircles)
            if (this->xSize > this->ySize)
            {
                // Horizontal stadium
                bounds.push_back(xo + 0.5 * (this->xSize - this->ySize)); // Lower-right point of rectangle determined by half the size difference
                bounds.push_back(yo - 0.5 * this->ySize); // Lower-right point of rectangle has simple y-coordinate of half the width
                for (size_t indi = 0; indi <= 12; indi++)
                {
                    bounds.push_back(xo + 0.5 * (this->xSize - this->ySize) + 0.5 * this->ySize * sin(2.0 * M_PI * indi / 24)); // x-coordinate has offset, rectangle point arithmetic, and semicircle radius determined by half the width
                    bounds.push_back(yo - 0.5 * this->ySize * cos(2.0 * M_PI * indi / 24)); // y-coordinate has offset and semicircle radius
                }
                bounds.push_back(xo - 0.5 * (this->xSize - this->ySize)); // Upper-left point of rectangle drawn from upper-right point where semicircle ends
                bounds.push_back(yo + 0.5 * this->ySize);
                for (size_t indi = 0; indi <= 12; indi++)
                {
                    bounds.push_back(xo - 0.5 * (this->xSize - this->ySize) - 0.5 * this->ySize * sin(2.0 * M_PI * indi / 24));
                    bounds.push_back(yo + 0.5 * this->ySize * cos(2.0 * M_PI * indi / 24));
                }
                bounds.push_back(xo + 0.5 * (this->xSize - this->ySize)); // Lower-right point of rectangle repeated from lower-left point where semicircle ends
                bounds.push_back(yo - 0.5 * this->ySize);
            }
            else if (this->ySize > this->xSize)
            {
                // Vertical stadium
                bounds.push_back(xo + 0.5 * this->xSize); // Lower-right point of rectangle has simple x-coordinate of half the length
                bounds.push_back(yo - 0.5 * (this->ySize - this->xSize)); // Lower-right point of rectangle determined by half the size difference
                bounds.push_back(xo + 0.5 * this->xSize); // Upper-right point of rectangle has simple x-coordinate of half the length
                bounds.push_back(yo + 0.5 * (this->ySize - this->xSize)); // Upper-right point of rectangle determined by half the size difference
                for (size_t indi = 0; indi <= 12; indi++)
                {
                    bounds.push_back(xo + 0.5 * this->xSize * cos(2.0 * M_PI * indi / 24)); // x-coordinate has offset and semicircle radius
                    bounds.push_back(yo + 0.5 * (this->ySize - this->xSize) + 0.5 * this->xSize * sin(2.0 * M_PI * indi / 24)); // y-coordinate has offset, rectangle point arithmetic, and semicircle radius determined by half the length
                }
                bounds.push_back(xo - 0.5 * this->xSize); // Lower-left point of rectangle drawn from upper-left point where semicircle ends
                bounds.push_back(yo - 0.5 * (this->ySize - this->xSize));
                for (size_t indi = 0; indi <= 12; indi++)
                {
                    bounds.push_back(xo - 0.5 * this->xSize * cos(2.0 * M_PI * indi / 24));
                    bounds.push_back(yo - 0.5 * (this->ySize - this->xSize) - 0.5 * this->xSize * sin(2.0 * M_PI * indi / 24));
                } // Lower-right point of rectangle repeated where semicircle ends
            } // Equality case handled by circle drawing
        }
        else if (this->stanTemp == 'P')
        {
            // Push back pairs of coordinates for the numVert + 1 polygon points CCW starting from phi=0+rotation
            for (size_t indi = 0; indi <= this->numVert; indi++)
            {
                bounds.push_back(xo + 0.5 * this->circumDia * cos(2.0 * M_PI * indi / this->numVert + this->rotation)); // x-coordinate
                bounds.push_back(yo + 0.5 * this->circumDia * sin(2.0 * M_PI * indi / this->numVert + this->rotation)); // y-coordinate
            }
        }

        // Create re-entrant part of boundary only if there is a valid hole
        if ((this->holeDia > 0) && (this->holeDia < this->circumDia))
        {
            // Approximate center hole as 24-gon starting and ending at the current rotation meticulously calculated for this purpose
            for (size_t indi = 0; indi <= 24; indi++)
            {
                bounds.push_back(xo + 0.5 * this->holeDia * cos(2.0 * M_PI * indi / 24 + this->rotation)); // x-coordinate
                bounds.push_back(yo + 0.5 * this->holeDia * sin(2.0 * M_PI * indi / 24 + this->rotation)); // y-coordinate
            }
        }

        // Return the complete boundary in preferred order
        boundary outBound = boundary(bounds, 1, { });
        outBound.reorder();
        return outBound;
    }

    // Print the aperture modifiers
    void print() const
    {
        cout << " ------" << endl;
        cout << " Aperture Modifiers:" << endl;
        cout << "  Aperture number: D" << this->aperNum << endl;
        cout << "  Standard aperture template: " << this->stanTemp << endl;
        if (this->isCircle())
        {
            cout << "  Aperture diameter: " << this->xSize << " m" << endl;
        }
        else if (this->isSquare())
        {
            cout << "  Maximum aperture extents of square: " << this->xSize << " m in x-direction and " << this->ySize << " m in y-direction" << endl;
            cout << "  Rotation angle of square (4 vertices): " << this->rotation << " rad" << endl;
        }
        else if (this->stanTemp == 'P')
        {
            cout << "  Maximum aperture extents: " << this->xSize << " m in x-direction and " << this->ySize << " m in y-direction" << endl;
            cout << "  Number of regular polygon vertices: " << this->numVert << endl;
            cout << "  Rotation angle of polygon: " << this->rotation << " rad" << endl;
        }
        else
        {
            cout << "  Maximum aperture extents: " << this->xSize << " m in x-direction and " << this->ySize << " m in y-direction" << endl;
        }
        cout << "  Diameter of center hole: " << this->holeDia << " m" << endl;
    }

    // Destructor
    ~Aperture()
    {
        // Nothing
    }
};

// Custom classes for containing solution and output Xyce/SPEF writer
class SimSettings
{
  private:
    double lengthUnit;     // Units for lengths (m)
    vector<double> limits; // xmin, xmax, ymin, ymax, zmin, zmax (m)
    double freqUnit;       // Units for frequency (Hz)
    double freqScale;      // Frequency scaling (0. for logarithmic [preferred], 1. for linear, otherwise undefined)
    size_t nFreq;          // Number of frequencies in simulation
    vector<double> freqs;  // List of frequencies (linear or logarithmic [preferred] spacing)
  public:
    // Default constructor
    SimSettings()
    {
        this->lengthUnit = 1.;
        this->limits = {0., 0., 0., 0., 0., 0.};
        this->freqUnit = 1.;
        this->freqScale = 0.;
        this->nFreq = 0;
        this->freqs = {};
    }

    // Parametrized constructor
    SimSettings(double lengthUnit, vector<double> limits, double freqUnit, double freqScale, vector<double> freqs)
    {
        this->lengthUnit = lengthUnit;
        if (limits.size() != 6)
        {
            cerr << "Must give minimum and maximum extents of design in vector of length 6. Defaulting to 0. to 0. for x, y, and z." << endl;
            this->limits = {0., 0., 0., 0., 0., 0.};
        }
        else
        {
            vector<double> checkLims = limits;
            if (limits[0] > limits[1]) // xmin > xmax
            {
                checkLims[0] = limits[1];
                checkLims[1] = limits[0];
            }
            if (limits[2] > limits[3]) // ymin > ymax
            {
                checkLims[2] = limits[3];
                checkLims[3] = limits[2];
            }
            if (limits[4] > limits[5]) // zmin > zmax
            {
                checkLims[4] = limits[5];
                checkLims[5] = limits[4];
            }
            this->limits = checkLims;
        }
        this->freqUnit = freqUnit;
        this->freqScale = freqScale;
        this->nFreq = freqs.size();
        this->freqs = freqs;
    }

    // Get length unit (m)
    double getLengthUnit() const
    {
        return this->lengthUnit;
    }

    // Get limits (minimum extent, maximum extent) for x-dir, y-dir, and z-dir
    vector<double> getLimits() const
    {
        return this->limits;
    }

    // Get frequency unit (Hz)
    double getFreqUnit() const
    {
        return this->freqUnit;
    }

    // Get frequency scaling
    double getFreqScale() const
    {
        return this->freqScale;
    }

    // Get number of frequencies in simulation
    size_t getNFreq() const
    {
        return this->nFreq;
    }

    // Get list of frequencies (linear or logarithmic [preferred] spacing)
    vector<double> getFreqs() const
    {
        return this->freqs;
    }

    // Get list of frequencies with frequency unit applied (Hz)
    vector<double> getFreqsHertz() const
    {
        vector<double> propFreqs = this->getFreqs();
        for (size_t indFreq = 0; indFreq < this->getNFreq(); indFreq++)
        {
            propFreqs[indFreq] *= this->getFreqUnit();
        }
        return propFreqs;
    }

    // Set length unit (m)
    void setLengthUnit(double lengthUnit)
    {
        this->lengthUnit = lengthUnit;
    }

    // Set limits: xmin, xmax, ymin, ymax, zmin, zmax (m)
    void setLimits(vector<double> limits)
    {
        if (limits.size() != 6)
        {
            cerr << "Must give minimum and maximum extents of design in vector of length 6. Defaulting to 0. to 0. for x, y, and z." << endl;
            this->limits = {0., 0., 0., 0., 0., 0.};
        }
        else
        {
            vector<double> checkLims = limits;
            if (limits[0] > limits[1]) // xmin > xmax
            {
                checkLims[0] = limits[1];
                checkLims[1] = limits[0];
            }
            if (limits[2] > limits[3]) // ymin > ymax
            {
                checkLims[2] = limits[3];
                checkLims[3] = limits[2];
            }
            if (limits[4] > limits[5]) // zmin > zmax
            {
                checkLims[4] = limits[5];
                checkLims[5] = limits[4];
            }
            this->limits = checkLims;
        }
    }

    // Set frequency unit (Hz)
    void setFreqUnit(double freqUnit)
    {
        this->freqUnit = freqUnit;
    }

    // Set frequency scaling
    void setFreqScale(double freqScale)
    {
        this->freqScale = freqScale;
    }

    // Set list of frequencies (linear or logarithmic [preferred] spacing)
    void setFreqs(vector<double> freqs)
    {
        this->freqs = freqs;
        this->nFreq = freqs.size();
    }

    // Print the simulation settings
    void print() const
    {
        size_t numFreq = this->getNFreq();
        cout << " ------" << endl;
        cout << " Simulation Settings:" << endl;
        cout << "  Limits in x-direction: " << (this->limits)[0] << " m to " << (this->limits)[1] << " m" << endl;
        cout << "  Limits in y-direction: " << (this->limits)[2] << " m to " << (this->limits)[3] << " m" << endl;
        cout << "  Limits in z-direction: " << (this->limits)[4] << " m to " << (this->limits)[5] << " m" << endl;
        cout << "  List of " << numFreq << " frequencies to simulate with " << this->freqScale << " scaling:" << endl;
        for (size_t indi = 0; indi < numFreq; indi++)
        {
            if (numFreq - indi == 1) // Single frequency left to print
            {
                cout << "   #" << indi + 1 << " is " << (this->freqs)[indi] * this->freqUnit << " Hz" << endl;
            }
            else // Two frequencies per line
            {
                cout << "   #" << indi + 1 << " is " << (this->freqs)[indi] * this->freqUnit << " Hz, and #" << indi + 2 << " is " << (this->freqs)[indi + 1] * this->freqUnit << " Hz" << endl;
                indi++;
            }
        }
    }

    // Destructor
    ~SimSettings()
    {
        // Nothing
    }
};

class Layer
{
  private:
    std::string layerName; // Name of layer in physical stack-up
    int gdsiiNum;          // Layer number in GDSII file
    double zStart;         // Z-coordinate of bottom of layer (m)
    double zHeight;        // Height of layer in z-direction (m)
    double epsilon_r;      // Relative permittivity of material
    double lossTan;        // Loss tangent of material
    double sigma;          // (Real) Conductivity of material (S/m)
  public:
    // Default constructor
    Layer()
    {
        this->layerName = "";
        this->gdsiiNum = -1;
        this->zStart = 0.;
        this->zHeight = 0.;
        this->epsilon_r = 1.;
        this->lossTan = 0.;
        this->sigma = 0.;
    }

    // Parametrized constructor
    Layer(std::string layerName, int gdsiiNum, double zStart, double zHeight, double epsilon_r, double lossTan, double sigma)
    {
        this->layerName = layerName;
        this->gdsiiNum = gdsiiNum;
        this->zStart = zStart;
        this->zHeight = zHeight;
        this->epsilon_r = epsilon_r;
        this->lossTan = lossTan;
        this->sigma = sigma;
    }

    // Get layer name
    std::string getLayerName() const
    {
        return this->layerName;
    }

    // Get GDSII file layer number
    // Metallic and dielectric layers are nonnegative, bottom plane is 0, top plane is MAX, and -1 is used for substrates and undescribed or nonphysical planes
    int getGDSIINum() const
    {
        return this->gdsiiNum;
    }

    // Get layer bottom z-coordinate
    // Currently assigns -1.0 for layers with unknown absolute position
    double getZStart() const
    {
        return this->zStart;
    }

    // Get layer height
    double getZHeight() const
    {
        return this->zHeight;
    }

    // Get layer relative permittivity
    double getEpsilonR() const
    {
        return this->epsilon_r;
    }

    // Get layer loss tangent
    double getLossTan() const
    {
        return this->lossTan;
    }

    // Get layer conductivity (S/m)
    double getSigma() const
    {
        return this->sigma;
    }

    // Set GDSII file layer number
    // Metallic and dielectric layers are nonnegative, bottom plane is 0, top plane is MAX, and -1 is used for substrates and undescribed or nonphysical planes
    void setGDSIINum(int gdsiiNum)
    {
        this->gdsiiNum = gdsiiNum;
    }

    // Return if the layer is valid and physical
    bool isValid() const
    {
        return ((this->zHeight > 0.0) && (this->epsilon_r >= 1.0) && (this->sigma >= 0.0)); // Positive height, permittivity that of free space or greater, and nonnegative conductivity
    }

    // Print the layer information
    void print() const
    {
        cout << "  Details for layer " << this->layerName << ":" << endl;
        if (this->gdsiiNum != -1)
        {
            cout << "   GDSII layer number: " << this->gdsiiNum << endl;
        }
        cout << "   Bottom z-coordinate: " << this->zStart << " m" << endl;
        cout << "   Layer height: " << this->zHeight << " m" << endl;
        cout << "   Relative permittivity: " << this->epsilon_r << endl;
        cout << "   Loss tangent: " << this->lossTan << endl;
        cout << "   Conductivity: " << this->sigma << " S/m" << endl;
        cout << "  ------" << endl;
    }

    // Destructor
    ~Layer()
    {
        // Nothing
    }
};

class Port
{
  private:
    std::string portName; // Name of port
    char portDir;         // Direction of port
    double Z_source;      // Impedance of source attached to port (ohm)
    int multiplicity;     // Number of simultaneous excitations needed for the port
    vector<double> coord; // Supply and return coordinates: xsup, ysup, zsup, xret, yret, zret (m, repeated multiplicity times)
    int gdsiiNum;         // Layer number in GDSII file on which port exists
  public:
    // Default constructor
    Port()
    {
        this->portName = "";
        this->portDir = 'B';
        this->Z_source = 0.;
        this->multiplicity = 1;
        this->coord = {0., 0., 0., 0., 0., 0.};
        this->gdsiiNum = -1;
    }

    // Parametrized constructor
    Port(std::string portName, char portDir, double Z_source, int multiplicity, vector<double> coord, int gdsiiNum)
    {
        this->portName = portName;
        if ((portDir != 'I') && (portDir != 'O') && (portDir != 'B'))
        {
            cerr << "Port direction must be assigned as 'I' (input), 'O' (output), or 'B' (bidirectional). Defaulting to 'B'." << endl;
            this->portDir = 'B';
        }
        else
        {
            this->portDir = portDir;
        }
        this->Z_source = Z_source;
        if (multiplicity < 1)
        {
            cerr << "Multiplicity of simultaenous port excitations must be at least 1. Defaulting to 1." << endl;
            this->multiplicity = 1;
        }
        else
        {
            this->multiplicity = multiplicity;
        }
        if (coord.size() != 6 * this->multiplicity)
        {
            cerr << "Must give supply then return coordinates in vector of length 6 * multiplicity for each side of the port. Defaulting to origin for all points." << endl;
            this->coord = vector<double>(this->multiplicity, 0.);
        }
        else
        {
            this->coord = coord;
            if (!this->validateCoord())
            {
                cerr << "Warning: Supply and return should only differ by a single coordinate for each port side. The program will run but may behave unusually." << endl;
            }
        }
        this->gdsiiNum = gdsiiNum;
    }

    // Get port name
    std::string getPortName() const
    {
        return this->portName;
    }

    // Get port direction
    // ('I' = input, 'O' = output, 'B' = bidirectional)
    char getPortDir() const
    {
        return this->portDir;
    }

    // Get impedance of attached source (ohm)
    double getZSource() const
    {
        return this->Z_source;
    }

    // Get number of simultaneous excitations needed for the port
    int getMultiplicity() const
    {
        return this->multiplicity;
    }

    // Get supply and return coordinates
    // xsup, ysup, zsup, xret, yret, zret (m, repeated multiplicity times)
    vector<double> getCoord() const
    {
        return this->coord;
    }

    // Get GDSII layer number of port
    // Metallic layers are nonnegative, bottom plane is 0, top plane is MAX, and -1 is used for dielectric, substrates, and undescribed planes
    int getGDSIINum() const
    {
        return this->gdsiiNum;
    }

    // Set port name
    void setPortName(std::string name)
    {
        this->portName = name;
    }

    // Set port direction
    // ('I' = input, 'O' = output, 'B' = bidirectional)
    void setPortDir(char dir)
    {
        if ((dir != 'I') && (dir != 'O') && (dir != 'B'))
        {
            cerr << "Port direction must be assigned as 'I' (input), 'O' (output), or 'B' (bidirectional). Defaulting to 'B'." << endl;
            this->portDir = 'B';
        }
        else
        {
            this->portDir = dir;
        }
    }

    // Set impedance of attached source
    void setZSource(double Z_source)
    {
        this->Z_source = Z_source;
    }

    // Set the port balanced condition
    void setMultiplicity(int multiplicity)
    {
        if (multiplicity < 1)
        {
            cerr << "Multiplicity of simultaenous port excitations must be at least 1. Defaulting to 1." << endl;
            this->multiplicity = 1;
        }
        else
        {
            this->multiplicity = multiplicity;
        }
    }

    // Set supply and return coordinates
    // xsup, ysup, zsup, xret, yret, zret (m, repeated multiplicity times)
    void setCoord(vector<double> coord)
    {
        if (coord.size() != 6 * this->multiplicity)
        {
            cerr << "Must give supply then return coordinates in vector of length 6 * multiplicity for each side of the port. Defaulting to origin for all points." << endl;
            this->coord = vector<double>(this->multiplicity, 0.);
        }
        else
        {
            this->coord = coord;
            if (!this->validateCoord())
            {
                cerr << "Warning: Supply and return should only differ by a single coordinate for each port side. The program will run but may behave unusually." << endl;
            }
        }
    }

    // Set GDSII layer number on which port exists
    // Metallic layers are nonnegative, bottom plane is 0, top plane is MAX, and -1 is used for dielectric, substrates, and undescribed planes
    void setGDSIINum(int gdsiiNum)
    {
        this->gdsiiNum = gdsiiNum;
    }

    // Validate port coordinates (supply and return change EXACTLY one Cartesian coordinate)
    bool validateCoord() const
    {
        bool validPort = true;
        for (size_t indMult = 0; indMult < this->getMultiplicity(); indMult++)
        {
            bool xEqual = this->coord[6 * indMult + 0] == this->coord[6 * indMult + 3];
            bool yEqual = this->coord[6 * indMult + 1] == this->coord[6 * indMult + 4];
            bool zEqual = this->coord[6 * indMult + 2] == this->coord[6 * indMult + 5];
            bool singleChange = (xEqual && yEqual && !zEqual) || (xEqual && !yEqual && zEqual) || (!xEqual && yEqual && zEqual); // Port side only changes one Cartesian coordinate from supply to return
            validPort = validPort && singleChange;
        }
        return validPort; // Port valid iff every port side only changes one Cartesian coordinate from supply to return
    }

    // Does the current flow in direction of increasing coordinate WITHIN the source? (+1 for yes, -1 for no, and 0 for bidirectional)
    vector<int> positiveCoordFlow() const
    {
        // Keep track of port side directionality effects
        vector<int> sideDir;
        int netPortEffect = 0;
        for (size_t indMult = 0; indMult < this->getMultiplicity(); indMult++)
        {
            // Logic: (xsup > xret) || (ysup > yret) || (zsup > zret) == TRUE means that at least one supply coordinate is strictly greater than the corresponding return coordinate
            // Logic: (xsup > xret) || (ysup > yret) || (zsup > zret) == FALSE means that all return coordinates are greater than or equal to the supply coordinates for the port side
            bool xSupGreater = this->coord[6 * indMult + 0] > this->coord[6 * indMult + 3];
            bool ySupGreater = this->coord[6 * indMult + 1] > this->coord[6 * indMult + 4];
            bool zSupGreater = this->coord[6 * indMult + 2] > this->coord[6 * indMult + 5];
            bool coordEffect = xSupGreater || ySupGreater || zSupGreater;

            sideDir.push_back(coordEffect ? +1 : -1); // The solver class just needs to know if Jx, Jy, or Jz is positive or negative within domain
            /*switch (this->portDir) // Decipher port side direction effect with ternary operator trickery
            {
            case 'O':
                // Output pin means that current flows out of source and FROM supply TO return within source
                // Logic for output pins: coord == TRUE means current flows in negative direction:   ret o-------<-------o sup
                // Logic for output pins: coord == FALSE means current flows in positive direction:   sup o------->-------o ret
                sideDir.push_back(coordEffect ? -1 : +1);
                netPortEffect += (coordEffect ? -1 : +1);
                break;
            case 'I':
                // Input pin means that current flows into source and FROM return TO supply within source
                // Logic for input pins: coord == TRUE means current flows in positive direction:   ret o------->-------o sup
                // Logic for input pins: coord == FALSE means current flows in negative direction:   sup o-------<-------o ret
                sideDir.push_back(coordEffect ? +1 : -1);
                netPortEffect += (coordEffect ? +1 : -1);
                break;
            case 'B':
                // Bidirectional pin means current can flow EITHER direction within source
                // Logic for bidirectional pins: match input pins since Z-parameters will be same for those ports
                sideDir.push_back(coordEffect ? +1 : -1);
                netPortEffect += (coordEffect ? +1 : -1);
                break;
            default:
                sideDir.push_back(0);
                netPortEffect += 0;
            }*/
        }

        // Return port side directionality for each side individually
        return sideDir;

        // // Interpret cumulative port directionality effects regardless of multiplicity
        // if (netPortEffect > 0)
        // {
        //     return +1;
        // }
        // else if (netPortEffect < 0)
        // {
        //     return -1;
        // }
        // else
        // {
        //     return 0;
        // }
    }

    // Print the port information
    void print() const
    {
        int mult = this->getMultiplicity();
        cout << "   ------" << endl;
        cout << "   Details for port " << this->portName << ":" << endl;
        cout << "    Port direction: ";
        switch (this->portDir)
        {
        case 'O':
            cout << "Output" << endl;
            break;
        case 'I':
            cout << "Input" << endl;
            break;
        case 'B':
            cout << "Bidirectional" << endl;
            break;
        default:
            cout << "Bidirectional" << endl; // Treat as bidirectional if direction unclear
        }
        cout << "    Attached source impedance: " << this->Z_source << " ohm" << endl;
        if (mult == 1)
        {
            cout << "    Supply coordinates: (" << (this->coord)[0] << ", " << (this->coord)[1] << ", " << (this->coord)[2] << ") m" << endl;
            cout << "    Return coordinates: (" << (this->coord)[3] << ", " << (this->coord)[4] << ", " << (this->coord)[5] << ") m" << endl;
        }
        else
        {
            for (size_t indMult = 0; indMult < mult; indMult++)
            {
                cout << "    Supply coordinates of side " << indMult + 1 << ": (" << (this->coord)[6 * indMult + 0] << ", " << (this->coord)[6 * indMult + 1] << ", " << (this->coord)[6 * indMult + 2] << ") m" << endl;
                cout << "    Return coordinates of side " << indMult + 1 << ": (" << (this->coord)[6 * indMult + 3] << ", " << (this->coord)[6 * indMult + 4] << ", " << (this->coord)[6 * indMult + 5] << ") m" << endl;
            }
        }
        cout << "    GDSII layer number: " << this->gdsiiNum << endl;
        cout << "   ------" << endl;
    }

    // Destructor
    ~Port()
    {
        // Nothing
    }
};

class Waveforms
{
  private:
    std::string name; // Placeholder name
  public:
    // Default constructor
    Waveforms()
    {
        this->name = "";
    }

    // Get name
    std::string getName() const
    {
        return this->name;
    }

    // Print the waveforms information
    void print() const
    {
        cout << " ------" << endl;
        cout << "  No Waveform Information" << endl;
        cout << " ------" << endl;
    }

    // Destructor
    ~Waveforms()
    {
        this->name = "";
    }
};

class Parasitics
{
  private:
    size_t nPorts;           // Number of ports
    vector<Port> ports;      // Vector of port information
    dMat matG;               // Nodal conductance matrix (S)
    dMat matC;               // Nodal capacitance matrix (F)
    vector<double> freqs;    // List of frequencies for network parameter matrix states (Hz)
    char param;              // Interpretation of network parameters
    vector<cdMat> matParam;  // Network parameter matrix at each frequency
  public:
    // Default constructor
    Parasitics()
    {
        vector<Port> ports;
        this->nPorts = 0;
        this->ports = ports;
        this->matG = dMat();
        this->matC = dMat();
        this->freqs = vector<double>();
        this->param = 'S'; // Default to S-parameters
        this->matParam = vector<cdMat>();
    }

    // Parametrized constructor for nodal admittance matrices (circuit construction)
    Parasitics(vector<Port> ports, dMat matG, dMat matC, vector<double> freqs)
    {
        this->nPorts = ports.size();
        this->ports = ports;
        this->matG = matG;
        this->matC = matC;
        this->freqs = freqs;
        this->param = 'Y';
        this->matParam = vector<cdMat>();
    }

    // Parametrized constructor for network parameters (port interactions)
    Parasitics(vector<Port> ports, vector<double> freqs, char param, vector<cdMat> matParam)
    {
        this->nPorts = ports.size();
        this->ports = ports;
        this->matG = dMat();
        this->matC = dMat();
        this->freqs = freqs;
        if ((param != 'S') && (param != 'Y') && (param != 'Z'))
        {
            cerr << "Network parameter matrix must be 'S' (scattering S-parameters), 'Y' (admittance Y-parameters), or 'Z' (impedance Z-parameters). Defaulting to 'S'." << endl;
            this->param = 'S';
        }
        else
        {
            this->param = param;
        }
        if (freqs.size() != matParam.size())
        {
            cerr << "Number of frequencies (" << freqs.size() << ") and number of network parameter matrix states (" << matParam.size() << ") do not match. Taking no action." << endl;
        }
        this->matParam = matParam;
    }

    // Get number of ports
    size_t getNPort() const
    {
        return this->nPorts;
    }

    // Get vector of port information
    vector<Port> getPorts() const
    {
        return this->ports;
    }

    // Get conductance matrix
    dMat getGMatrix() const
    {
        return this->matG;
    }

    // Get capacitance matrix
    dMat getCMatrix() const
    {
        return this->matC;
    }

    // Get list of frequencies (Hz)
    vector<double> getFreqs() const
    {
        return this->freqs;
    }

    // Get interpretation of network parameters matrix
    // ('S' = scattering, 'Y' = admittance, 'Z' = impedance)
    char getParamType() const
    {
        return this->param;
    }

    // Get network parameter matrix
    vector<cdMat> getParamMatrix() const
    {
        return this->matParam;
    }

    // Set vector of port information
    void setPorts(vector<Port> ports)
    {
        this->ports = ports;
        this->nPorts = ports.size();
    }

    // Set conductance matrix (S)
    void setGMatrix(dMat matG)
    {
        this->matG = matG;
    }

    // Set capacitance matrix (F)
    void setCMatrix(dMat matC)
    {
        this->matC = matC;
    }

    // Set list of frequencies (Hz)
    void setFreqs(vector<double> freqs)
    {
        if (freqs.size() != this->matParam.size())
        {
            cerr << "Number of frequencies (" << freqs.size() << ") and number of network parameter matrix states (" << this->matParam.size() << ") do not match. Taking no action." << endl;
        }
        this->freqs = freqs;
    }

    // Set interpretation of network parameters matrix
    // ('S' = scattering, 'Y' = admittance, 'Z' = impedance)
    void setParamType(char param)
    {
        if ((param != 'S') && (param != 'Y') && (param != 'Z'))
        {
            cerr << "Network parameter matrix must be 'S' (scattering S-parameters), 'Y' (admittance Y-parameters), or 'Z' (impedance Z-parameters). Defaulting to 'S'." << endl;
            this->param = 'S';
        }
        else
        {
            this->param = param;
        }
    }

    // Set network parameter matrix
    void setParamMatrix(vector<cdMat> matParam)
    {
        if (this->freqs.size() != matParam.size())
        {
            cerr << "Number of frequencies (" << this->freqs.size() << ") and number of network parameter matrix states (" << matParam.size() << ") do not match. Taking no action." << endl;
        }
        this->matParam = matParam;
    }

    // Find index of port by name
    // Returns index past number of ports if not found
    size_t locatePortName(std::string name) const
    {
        size_t indPort;
        for (indPort = 0; indPort < this->getNPort(); indPort++)
        {
            if (name.compare((this->ports[indPort]).getPortName()) == 0)
            {
                return indPort;
            }
        }
        return indPort;
    }

    // Return a port
    Port getPort(size_t indPort) const
    {
        return (this->ports)[indPort];
    }

    // Return all port names
    vector<std::string> findPortNames() const
    {
        vector<std::string> names;
        for (size_t indi = 0; indi < this->getNPort(); indi++)
        {
            names.push_back(((this->ports)[indi]).getPortName());
        }
        return names;
    }

    // Find ports in GDSII textboxes
    vector<Port> findPortsGDSII(size_t indCell, vector<double> center, strans transform, AsciiDataBase adb)
    {
        // Error checking on input point
        double xo, yo; // Coordinate offsets
        if (center.size() != 2)
        {
            cerr << "Coordinates of reference frame center must be a length-2 vector. Defaulting to (0, 0)." << endl;
            xo = 0.;
            yo = 0.;
        }
        else
        {
            xo = center[0];
            yo = center[1];
        }

        // Recursively search cells for textboxes
        GeoCell thisCell = adb.getCell(indCell);
        vector<Port> portList;
        for (size_t indi = 0; indi < thisCell.getNumSRef(); indi++) // Follow structure references
        {
            string srefName = (thisCell.sreferences)[indi].getSRefName();
            size_t indNextCell = adb.locateCell(srefName);
            vector<double> refPt = transform.applyTranform((thisCell.sreferences)[indi].getSRefs()); // Center point of structure reference after linear transformation
            vector<Port> newPorts = this->findPortsGDSII(indNextCell, { refPt[0] + xo, refPt[1] + yo }, (thisCell.sreferences)[indi].getTransform().composeTransform(transform), adb); // Recursion step
            portList.insert(portList.end(), newPorts.begin(), newPorts.end());
        }
        for (size_t indi = 0; indi < thisCell.getNumARef(); indi++) // Follow array references
        {
            string arefName = (thisCell.areferences)[indi].getARefName();
            size_t indNextCell = adb.locateCell(arefName);
            vector<vector<double>> instanceCoord = (thisCell.areferences[indi]).findInstances({ 0., 0. });
            for (size_t indj = 0; indj < instanceCoord.size(); indj++) // Handle each instance in array reference
            {
                vector<double> centPt = transform.applyTranform(instanceCoord[indj]); // Center point of referred instance after linear transformation
                vector<Port> newPorts = this->findPortsGDSII(indNextCell, { centPt[0] + xo, centPt[1] + yo }, (thisCell.areferences)[indi].getTransform().composeTransform(transform), adb); // Recursion step
                portList.insert(portList.end(), newPorts.begin(), newPorts.end());
            }
        }

        // Search cell at this level for textboxes
        for (size_t indi = 0; indi < thisCell.getNumText(); indi++)
        {
            textbox thisTextBox = thisCell.textboxes[indi];
            vector<double> coords = { thisTextBox.getTexts()[0] + xo, thisTextBox.getTexts()[1] + yo, 0., thisTextBox.getTexts()[0] + xo, 0., 0. }; // Assume: xret = xsup, yret = 0, zret = zsup = 0 until layer heights set
            portList.emplace_back(Port(thisTextBox.getTextStr(), 'B', 50.0, 1, coords, thisTextBox.getLayer())); // Assume all ports have multiplicity 1
        }

        // Return vector of port information
        return portList;
    }

    // Return node-to-ground conductance (S)
    double getGNodeGround(size_t indNode) const
    {
        // Calculate by summing along given row
#ifdef EIGEN_SPARSE
        return (this->matG).innerVector(indNode).sum();
#else
        return this->matG.rowwise().sum()[indNode];
#endif
    }

    // Return total conductance represented in matrix (S)
    double getGTotal() const
    {
        dMat matGUpper = (this->matG).triangularView<Eigen::Upper>(); // Upper triangular part of conductance matrix (S)
        double GTot = 0.0; // Total conductance in matrix (S)
        for (size_t indi = 0; indi < matGUpper.outerSize(); indi++) // Loop only over upper triangular portion
        {
#ifdef EIGEN_SPARSE
            for (spMat::InnerIterator it(matGUpper, indi); it; ++it)
            {
                /* Diagonal elements have total conductance attached to the node.
                The sum of the main diagonal by itself (trace) would double count the off-diagonal conductances.
                The trace plus the strictly upper triangular elements would remove the second count, making only the upper triangular sum needed. */
                GTot += it.value();
            }
#else
            for (size_t indj = 0; indj < matGUpper.innerSize(); indj++)
            {
                /* Diagonal elements have total conductance attached to the node.
                The sum of the main diagonal by itself (trace) would double count the off-diagonal conductances.
                The trace plus the strictly upper triangular elements would remove the second count, making only the upper triangular sum needed. */
                GTot += matGUpper(indi, indj);
            }
#endif
        }
        return GTot;
    }

    // Is conductance matrix reciprocal (is matrix symmetric)?
    bool isGRecip() const
    {
        return this->matG.isApprox(this->matG.transpose());
    }

    // Make conductance matrix reciprocal
    void makeGSym()
    {
        dMat symG = this->matG; // Start with original conductance matrix (S)
        if (!this->isGRecip())
        {
            symG += dMat(this->matG.transpose()); // Copy constructor ensures sparse matrices have same storage order for addition
            symG *= 0.5;
            this->setGMatrix(symG); // Overwrite the conductance matrix with the symmetrized one
        }
    }

    // Return node-to-ground capacitance (F)
    double getCNodeGround(size_t indNode) const
    {
        // Calculate by summing along given row
#ifdef EIGEN_SPARSE
        return (this->matC).innerVector(indNode).sum();
#else
        return this->matC.rowwise().sum()[indNode];
#endif
    }

    // Return total capacitance represented in matrix (F)
    double getCTotal() const
    {
        dMat matCUpper = (this->matC).triangularView<Eigen::Upper>(); // Upper triangular part of capacitance matrix (F)
        double CTot = 0.0; // Total capacitance in matrix (F)
        for (size_t indi = 0; indi < matCUpper.outerSize(); indi++) // Loop only over upper triangular portion
        {
#ifdef EIGEN_SPARSE
            for (spMat::InnerIterator it(matCUpper, indi); it; ++it)
            {
                /* Diagonal elements have total capacitance attached to the node.
                The sum of the main diagonal by itself (trace) would double count the off-diagonal capacitances.
                The trace plus the strictly upper triangular elements would remove the second count, making only the upper triangular sum needed. */
                CTot += it.value();
            }
#else
            for (size_t indj = 0; indj < matCUpper.innerSize(); indj++)
            {
                /* Diagonal elements have total capacitance attached to the node.
                The sum of the main diagonal by itself (trace) would double count the off-diagonal capacitances.
                The trace plus the strictly upper triangular elements would remove the second count, making only the upper triangular sum needed. */
                CTot += matCUpper(indi, indj);
            }
#endif
        }
        return CTot;
    }

    // Is capacitance matrix reciprocal (is matrix symmetric)?
    bool isCRecip() const
    {
        return this->matC.isApprox(this->matC.transpose());
    }

    // Make capacitance matrix reciprocal
    void makeCSym()
    {
        dMat symC = this->matC; // Start with original capacitance matrix (F)
        if (!this->isCRecip())
        {
            symC += dMat(this->matC.transpose()); // Copy constructor ensures sparse matrices have same storage order for addition
            symC *= 0.5;
            this->setCMatrix(symC); // Overwrite the capacitance matrix with the symmetrized one
        }
    }

    // Save network parameters as a sparse matrix (retrieve from fdtdMesh)
    void saveNetworkParam(char param, vector<double> freqs, vector<complex<double>> p)
    {
        // Parameter storage
        size_t nPorts = this->getNPort();
        if (nPorts * nPorts * freqs.size() != p.size())
        {
            cerr << "The number of network parameter evaluated entries (" << p.size() << ") does not match the number of ports squared times the frequency points (" << nPorts * nPorts * freqs.size() << "). Attempting execution anyways." << endl;
        }
        for (size_t indFreq = 0; indFreq < this->freqs.size(); indFreq++)
        {
            // Build up triplet list for network parameter matrix
            //cout << "Saving " << param << "-parameters evaluated at f=" << freqs[indFreq] << " Hz" << endl;
#ifdef EIGEN_SPARSE
            vector<cdTriplet> listP;
            listP.reserve(nPorts * nPorts); // Reserve room so matrix could be dense
#else
            cdMat thisP(nPorts, nPorts); // Initialize Eigen complex matrix for this frequency
#endif
            for (size_t indi = 0; indi < nPorts; indi++) // Loop over excitation ports (each row)
            {
                for (size_t indj = 0; indj < nPorts; indj++) // Loop over response ports (each column)
                {
                    if ((param == 'Y') || (param == 'Z') || (param == 'S'))
                    {
                        // Save network parameters
#ifdef EIGEN_SPARSE
                        listP.push_back(cdTriplet(indi, indj, p[(indFreq * nPorts + indi) * nPorts + indj])); // Matrix entry
#else
                        thisP(indi, indj) = p[(indFreq * nPorts + indi) * nPorts + indj]; // Matrix entry
#endif
                    }
                    else
                    {
                        cerr << "Unrecognized network parameters. Breaking now." << endl;
                        return;
                    }
                }
            }

#ifdef EIGEN_SPARSE
            // Generate the sparse network parameter matrix from nonzero entries
            cspMat thisP(nPorts, nPorts); // Initialize Eigen complex sparse matrix for this frequency
            thisP.setFromTriplets(listP.begin(), listP.end());
#ifdef EIGEN_COMPRESS
            thisP.makeCompressed(); // Save memory by removing room for inserting new entries
#endif
#endif

            // Update an object in this class with this network parameters evaluation
            this->matParam.push_back(thisP);
        }

        // Save information to class
        this->setParamType(param);
        this->setFreqs(freqs);

        // Generate nodal admittance matrices
        this->computeYBusFromParam(0); // Draw circuit based on the lowest frequency present
    }

    // Convert existing network parameters to different type
    // ('S' = scattering, 'Y' = admittance, 'Z' = impedance)
    void convertParam(char newParam)
    {
        // Orignally had admittance parameters
        if (this->param == 'Y')
        {
            if (newParam == 'Y')
            {
                //cout << "No conversion needed to get Y-parameters." << endl;
            }
            else if (newParam == 'Z')
            {
                cout << "Notice: Finding Z-parameters by inverting Y-parameters matrix." << endl;
                for (size_t indFreq = 0; indFreq < this->freqs.size(); indFreq++)
                {
                    cdMat matZ = this->matParam[indFreq].inverse();
                    this->matParam[indFreq] = matZ;
                }
                this->setParamType('Z');
            }
            else if (newParam == 'S')
            {
                cout << "Notice: Finding S-parameters from Y-parameters with port impedances, matrix arithmetic, and slow matrix inversion." << endl;
                const int nPorts = (const int)this->getNPort();
                cdMat matEye = cdMat::Identity(nPorts, nPorts); // Initialize, size, and set the entries of the identity matrix
                Eigen::VectorXd diagPortZ(nPorts); // Declare a vector to hold square roots of the port characteristic impedances
                for (size_t indPort = 0; indPort < nPorts; indPort++)
                {
                    diagPortZ(indPort) = sqrt(this->ports[indPort].getZSource());
                }
                Eigen::DiagonalMatrix<complex<double>, Eigen::Dynamic> diagZroot(nPorts); // Declare the diagonal matrix
                diagZroot.diagonal() = diagPortZ; // Set the entries of the diagonal matrix
                for (size_t indFreq = 0; indFreq < this->freqs.size(); indFreq++)
                {
                    cdMat matLFactor(nPorts, nPorts);
                    matLFactor = matEye - diagZroot * this->matParam[indFreq] * diagZroot; // Left factor
                    cdMat matRFactor(nPorts, nPorts);
                    matRFactor = matEye + diagZroot * this->matParam[indFreq] * diagZroot; // Right factor prior to matrix inversion
                    cdMat matS = matLFactor * matRFactor.inverse();
                    this->matParam[indFreq] = matS;
                }
                this->setParamType('S');
            }
            else
            {
                cerr << "New network parameter matrix must be 'S' (scattering S-parameters), 'Y' (admittance Y-parameters), or 'Z' (impedance Z-parameters). Taking no action." << endl;
            }
        }

        // Originally had impedance parameters
        else if (this->param == 'Z')
        {
            if (newParam == 'Y')
            {
                cout << "Notice: Finding Y-parameters by inverting Z-parameters matrix." << endl;
                for (size_t indFreq = 0; indFreq < this->freqs.size(); indFreq++)
                {
                    cdMat matY = this->matParam[indFreq].inverse();
                    this->matParam[indFreq] = matY;
                }
                this->setParamType('Y');
            }
            else if (newParam == 'Z')
            {
                //cout << "No conversion needed to get Z-parameters." << endl;
            }
            else if (newParam == 'S')
            {
                cout << "Notice: Finding S-parameters from Z-parameters with port admittances, matrix arithmetic, and slow matrix inversion." << endl;
                const int nPorts = (const int)this->getNPort();
                cdMat matEye = cdMat::Identity(nPorts, nPorts); // Initialize, size, and set the entries of the identity matrix
                Eigen::VectorXd diagPortY(nPorts); // Declare a vector to hold square roots of the port characteristic admittances
                for (size_t indPort = 0; indPort < nPorts; indPort++)
                {
                    diagPortY(indPort) = sqrt(1.0 / this->ports[indPort].getZSource());
                }
                Eigen::DiagonalMatrix<complex<double>, Eigen::Dynamic> diagYroot(nPorts); // Declare the diagonal matrix
                diagYroot.diagonal() = diagPortY; // Set the entries of the diagonal matrix
                for (size_t indFreq = 0; indFreq < this->freqs.size(); indFreq++)
                {
                    cdMat matLFactor(nPorts, nPorts);
                    matLFactor = diagYroot * this->matParam[indFreq] * diagYroot - matEye; // Left factor
                    cdMat matRFactor(nPorts, nPorts);
                    matRFactor = diagYroot * this->matParam[indFreq] * diagYroot + matEye; // Right factor prior to matrix inversion
                    cdMat matS = matLFactor * matRFactor.inverse();
                    this->matParam[indFreq] = matS;
                }
                this->setParamType('S');
            }
            else
            {
                cerr << "New network parameter matrix must be 'S' (scattering S-parameters), 'Y' (admittance Y-parameters), or 'Z' (impedance Z-parameters). Taking no action." << endl;
            }
        }

        // Originally had scattering parameters
        else if (this->param == 'S')
        {
            if (newParam == 'Y')
            {
                cout << "Notice: Finding Y-parameters from S-parameters with port admittances, matrix arithmetic, and slow matrix inversion." << endl;
                const int nPorts = (const int)this->getNPort();
                cdMat matEye = cdMat::Identity(nPorts, nPorts); // Initialize, size, and set the entries of the identity matrix
                Eigen::VectorXd diagPortY(nPorts); // Declare a vector to hold square roots of the port characteristic admittances
                for (size_t indPort = 0; indPort < nPorts; indPort++)
                {
                    diagPortY(indPort) = sqrt(1.0 / this->ports[indPort].getZSource());
                }
                Eigen::DiagonalMatrix<complex<double>, Eigen::Dynamic> diagYroot(nPorts); // Declare the diagonal matrix
                diagYroot.diagonal() = diagPortY; // Set the entries of the diagonal matrix
                for (size_t indFreq = 0; indFreq < this->freqs.size(); indFreq++)
                {
                    cdMat matLFactor(nPorts, nPorts);
                    matLFactor = matEye - this->matParam[indFreq]; // Inner-left factor
                    cdMat matRFactor(nPorts, nPorts);
                    matRFactor = matEye + this->matParam[indFreq]; // Inner-right factor prior to matrix inversion
                    cdMat matY = diagYroot * matLFactor * matRFactor.inverse() * diagYroot;
                    this->matParam[indFreq] = matY;
                }
                this->setParamType('Y');
            }
            else if (newParam == 'Z')
            {
                cout << "Notice: Finding Z-parameters from S-parameters with port impedances, matrix arithmetic, and slow matrix inversion." << endl;
                const int nPorts = (const int)this->getNPort();
                cdMat matEye = cdMat::Identity(nPorts, nPorts); // Initialize, size, and set the entries of the identity matrix
                Eigen::VectorXd diagPortZ(nPorts); // Declare a vector to hold square roots of the port characteristic impedances
                for (size_t indPort = 0; indPort < nPorts; indPort++)
                {
                    diagPortZ(indPort) = sqrt(this->ports[indPort].getZSource());
                }
                Eigen::DiagonalMatrix<complex<double>, Eigen::Dynamic> diagZroot(nPorts); // Declare the diagonal matrix
                diagZroot.diagonal() = diagPortZ; // Set the entries of the diagonal matrix
                for (size_t indFreq = 0; indFreq < this->freqs.size(); indFreq++)
                {
                    cdMat matLFactor(nPorts, nPorts);
                    matLFactor = matEye + this->matParam[indFreq]; // Inner-left factor
                    cdMat matRFactor(nPorts, nPorts);
                    matRFactor = matEye - this->matParam[indFreq]; // Inner-right factor prior to matrix inversion
                    cdMat matZ = diagZroot * matLFactor * matRFactor.inverse() * diagZroot;
                    this->matParam[indFreq] = matZ;
                }
                this->setParamType('Z');
            }
            else if (newParam == 'S')
            {
                //cout << "No conversion needed to get S-parameters." << endl;
            }
            else
            {
                cerr << "New network parameter matrix must be 'S' (scattering S-parameters), 'Y' (admittance Y-parameters), or 'Z' (impedance Z-parameters). Taking no action." << endl;
            }
        }

        // Originally had unrecognized parameters
        else
        {
            cerr << "Original network parameter matrix unexpectedly was neither 'S' (scattering S-parameters), 'Y' (admittance Y-parameters), nor 'Z' (impedance Z-parameters). Taking no action." << endl;
        }
    }

    // Compute admittance parameters from nodal admittance matrices
    void computeYParamFromCircuit()
    {
        // Set network parameter matrix interpretation
        this->param = 'Y';

#ifdef EIGEN_SPARSE
        // Parameter storage
        size_t nPorts = this->getNPort();
        vector<cdTriplet> listG; // Initialize first of two triplet lists for admittance matrix
        listG.reserve(nPorts * nPorts); // Reserve room so matrix could be dense

        // Matrix entry real-part calculation (frequency-independent)
        dMat matG = this->getGMatrix(); // Nodal conductance sparse matrix (S)
        for (size_t indi = 0; indi < matG.outerSize(); indi++) // Loop over excitation ports (each row)
        {
            for (spMat::InnerIterator it(matG, indi); it; ++it) // Loop over response ports (each column)
            {
                // First pass sets conductance as real part of admittance (S)
                listG.push_back(cdTriplet(indi, it.col(), complex<double>(it.value(), 0.0)));
            }
        }

        // Matrix entry imaginary-part calculation (frequency-dependent) and construction
        dMat matC = this->getCMatrix(); // Nodal capacitance sparse matrix (F)
        for (size_t indFreq = 0; indFreq < this->freqs.size(); indFreq++)
        {
            vector<cdTriplet> listB; // Initialize second of two triplet lists for admittance matrix
            listB.reserve(2 * nPorts * nPorts); // Reserve room so matrix could be dense after copying another list
            for (size_t indi = 0; indi < matC.outerSize(); indi++) // Loop over excitation ports (each row)
            {
                for (spMat::InnerIterator it(matC, indi); it; ++it) // Loop over response ports (each column)
                {
                    // Second pass sets susceptance as imaginary part of admittance for each frequency (S)
                    listB.push_back(cdTriplet(indi, it.col(), complex<double>(0.0, 2. * M_PI * this->freqs[indFreq] * it.value())));
                }
            }

            // Concatenate the two vectors of triplets by copying (preserves conductances already found once)
            listB.insert(listB.end(), listG.begin(), listG.end());

            // Generate the sparse Y-parameter matrix from nonzero entries (anonymous function has duplicate entries added)
            cdMat thisY(nPorts, nPorts); // Initialize Eigen complex sparse admittance matrix for this frequency (S)
            thisY.setFromTriplets(listB.begin(), listB.end(), [](const complex<double> &a, const complex<double> &b) { return a + b; });
#ifdef EIGEN_COMPRESS
            thisY.makeCompressed(); // Save memory by removing room for inserting new entries
#endif
#else
        // Parameter storage
        size_t nPorts = this->getNPort();

        // Complex matrix construction (frequency-dependent)
        for (size_t indFreq = 0; indFreq < this->freqs.size(); indFreq++)
        {
            cdMat thisY(nPorts, nPorts); // Initialize Eigen complex matrix
            thisY = this->getGMatrix() + complex<double>(0.0, 1.0) * 2. * M_PI * this->freqs[indFreq] * this->getCMatrix();
#endif

            // Update an object in this class with this Y-parameter evaluation
            this->matParam.push_back(thisY);
        }
    }

    // Compute nodal admittance matrices from network parameters at a certain frequency
    void computeYBusFromParam(size_t indFreq)
    {
        // Need to get Y-parameters temporarily
        const int nPorts = (const int)this->getNPort();
        cdMat matY;
        switch (this->param)
        {
        case ('Y'):
        {
            cout << "Directly using Y-parameters to place circuit elements." << endl;
            matY = this->matParam[indFreq];
            break;
        }
        case ('Z'):
        {
            cout << "Notice: Finding Y-parameters by inverting Z-parameters matrix. Placing circuit elements afterwards." << endl;
            matY = this->matParam[indFreq].inverse();
            break;
        }
        case ('S'):
        {
            cout << "Notice: Finding Y-parameters from S-parameters with port admittances, matrix arithmetic, and slow matrix inversion." << endl;
            const int nPorts = (const int)this->getNPort();
            cdMat matEye = cdMat::Identity(nPorts, nPorts); // Initialize, size, and set the entries of the identity matrix
            Eigen::VectorXd diagPortY(nPorts); // Declare a vector to hold square roots of the port characteristic admittances
            for (size_t indPort = 0; indPort < nPorts; indPort++)
            {
                diagPortY(indPort) = sqrt(1.0 / this->ports[indPort].getZSource());
            }
            Eigen::DiagonalMatrix<complex<double>, Eigen::Dynamic> diagYroot(nPorts); // Declare the diagonal matrix
            diagYroot.diagonal() = diagPortY; // Set the entries of the diagonal matrix
            for (size_t indFreq = 0; indFreq < this->freqs.size(); indFreq++)
            {
                cdMat matLFactor(nPorts, nPorts);
                matLFactor = matEye - this->matParam[indFreq]; // Inner-left factor
                cdMat matRFactor(nPorts, nPorts);
                matRFactor = matEye + this->matParam[indFreq]; // Inner-right factor prior to matrix inversion
                cdMat matY = diagYroot * matLFactor * matRFactor.inverse() * diagYroot;
                this->matParam[indFreq] = matY;
            }
            break;
        }
    }

#ifdef EIGEN_SPARSE
        // Parameter storage
        vector<dTriplet> listG, listC; // Initialize triplet lists for conductance and capacitance matrices
        listG.reserve(nPorts * nPorts); // Reserve room so matrix could be dense
        listC.reserve(nPorts * nPorts);

        // Matrix entries calculation
        for (size_t indi = 0; indi < matY.outerSize(); indi++) // Loop over excitation ports (each row)
        {
            for (cspMat::InnerIterator it(matY, indi); it; ++it) // Loop over response ports (each column)
            {
                listG.push_back(dTriplet(indi, it.col(), it.value().real())); // gij = Re{yij} (S)
                listC.push_back(dTriplet(indi, it.col(), it.value().imag() / (2. * M_PI * this->freqs[indFreq]))); // cij = bij / omega = Im{yij} / (2*pi*f) (F)
            }
        }

        // Generate the sparse nodal admittance matrices from nonzero entries
        dMat matG(nPorts, nPorts); // Initialize Eigen sparse nodal conductance matrix for this frequency (S)
        dMat matC(nPorts, nPorts); // Initialize Eigen sparse nodal capacitance matrix for this frequency (F)
        matG.setFromTriplets(listG.begin(), listG.end());
        matC.setFromTriplets(listC.begin(), listC.end());
#ifdef EIGEN_COMPRESS
        matG.makeCompressed(); // Save memory by removing room for inserting new entries
        matC.makeCompressed();
#endif
#else
        // Matrix calculation
        dMat matG(nPorts, nPorts); // Initialize Eigen nodal conductance matrix for this frequency (S)
        dMat matC(nPorts, nPorts); // Initialize Eigen nodal capacitance matrix for this frequency (F)
        matG = matY.real(); // G = Re{Y} (S)
        matC = matY.imag() / (2. * M_PI * this->freqs[indFreq]); // C = B / omega = Im{Y} / (2*pi*f) (F)
#endif
        this->setGMatrix(matG); // Update the objects in this class
        this->setCMatrix(matC);
    }

    // Terminate port of Z-parameter matrix with known impedance for all frequencies
    void terminatePortZ(size_t indPort, vector<complex<double>> ZLoad)
    {
        // Check the input
        if (this->param != 'Z')
        {
            cerr << "Unable to terminate port with impedance because Z-parameters are not stored. Breaking now." << endl;
            return;
        }
        size_t nPorts = this->getNPort(); // Number of ports
        if (indPort >= nPorts)
        {
            cerr << "Port index for impedance termination greater than matrix size. Breaking now." << endl;
            return;
        }
        vector<complex<double>> ZL; // Initialize port impedance response (ohm)
        if ((ZLoad.size() == 1) || (this->freqs.size() != 1))
        {
            cout << "Interpolating port termination impedance from starting frequency to all subsequent frequencies, assuming inductive behavior." << endl;
            for (size_t indFreq = 0; indFreq < this->freqs.size(); indFreq++)
            {
                ZL.push_back(complex<double>(ZLoad[0].real(), ZLoad[0].imag() * this->freqs[indFreq] / this->freqs[0]));
            }
        }
        else if (ZLoad.size() != this->freqs.size())
        {
            cerr << "Number of port termination impedance evaluations does not match the frequency evaluations of the network parameters. Breaking now." << endl;
            return;
        }
        else
        {
            ZL = ZLoad; // Use the port termination impedance evaluations directly (ohm)
        }

        // Kron reduction of system of linear equations
        // zij -> zij - (zik * zkj) / (zkk + Zp); where Zp is the port load impedance and k is the index of the loaded port not affecting the i,j numbers
        for (size_t indFreq = 0; indFreq < this->freqs.size(); indFreq++)
        {
            cdMat newZ(nPorts - 1, nPorts - 1);
            for (size_t indi = 0; indi < nPorts; indi++)
            {
                for (size_t indj = indi; indj < nPorts; indj++) // Index the upper triangular elements
                {
                    // Index manipulation for reduced Z-parameters matrix
                    size_t newi = indi;
                    size_t newj = indj;
                    if (indi = indPort)
                    {
                        continue; // Nothing to do for this row
                    }
                    else if (indi > indPort)
                    {
                        newi--; // Adjust index of subsequent rows in reduced Z-parameters matrix
                    }
                    if (indj = indPort)
                    {
                        continue; // Nothing to do for this column
                    }
                    else if (indj > indPort)
                    {
                        newi--; // Adjust index of subsequent columns in reduced Z-parameters matrix
                    }

                    // Kron reduction step for entries in symmetric positions
                    newZ(newi, newj) = this->matParam[indFreq](indi, indj) - (this->matParam[indFreq](indi, indPort) * this->matParam[indFreq](indPort, indj)) / (this->matParam[indFreq](indPort, indPort) + ZL[indFreq]);
                }
            }

            // Update saved network parameters evaluation information
            this->matParam[indFreq] = newZ;
        }

        // Update saved ports information
        this->ports.erase(this->ports.begin() + indPort); // Remove the port
        this->nPorts--; // Adjust number of ports
    }

    // Terminate port of Y-parameter matrix with known admittance for all frequencies
    void terminatePortY(size_t indPort, vector<complex<double>> YLoad)
    {
        // Check the input
        if (this->param != 'Y')
        {
            cerr << "Unable to terminate port with admittance because Y-parameters are not stored. Breaking now." << endl;
            return;
        }
        size_t nPorts = this->getNPort(); // Number of ports
        if (indPort >= nPorts)
        {
            cerr << "Port index for admittance termination greater than matrix size. Breaking now." << endl;
            return;
        }
        vector<complex<double>> YL; // Initialize port admittance response (S)
        if ((YLoad.size() == 1) || (this->freqs.size() != 1))
        {
            cout << "Interpolating port termination admittance from starting frequency to all subsequent frequencies, assuming capacitive behavior." << endl;
            for (size_t indFreq = 0; indFreq < this->freqs.size(); indFreq++)
            {
                YL.push_back(complex<double>(YLoad[0].real(), YLoad[0].imag() * this->freqs[indFreq] / this->freqs[0]));
            }
        }
        else if (YLoad.size() != this->freqs.size())
        {
            cerr << "Number of port termination admittance evaluations does not match the frequency evaluations of the network parameters. Breaking now." << endl;
            return;
        }
        else
        {
            YL = YLoad; // Use the port termination admittance evaluations directly (S)
        }

        // Kron reduction of system of linear equations
        // yij -> yij - (yik * ykj) / (ykk + Yp); where Yp is the port load admittance and k is the index of the loaded port not affecting the i,j numbers
        for (size_t indFreq = 0; indFreq < this->freqs.size(); indFreq++)
        {
            cdMat newY(nPorts - 1, nPorts - 1);
            for (size_t indi = 0; indi < nPorts; indi++)
            {
                for (size_t indj = indi; indj < nPorts; indj++) // Index the upper triangular elements
                {
                    // Index manipulation for reduced Y-parameters matrix
                    size_t newi = indi;
                    size_t newj = indj;
                    if (indi = indPort)
                    {
                        continue; // Nothing to do for this row
                    }
                    else if (indi > indPort)
                    {
                        newi--; // Adjust index of subsequent rows in reduced Y-parameters matrix
                    }
                    if (indj = indPort)
                    {
                        continue; // Nothing to do for this column
                    }
                    else if (indj > indPort)
                    {
                        newi--; // Adjust index of subsequent columns in reduced Y-parameters matrix
                    }

                    // Kron reduction step for entries in symmetric positions
                    newY(newi, newj) = this->matParam[indFreq](indi, indj) - (this->matParam[indFreq](indi, indPort) * this->matParam[indFreq](indPort, indj)) / (this->matParam[indFreq](indPort, indPort) + YL[indFreq]);
                }
            }

            // Update saved network parameters evaluation information
            this->matParam[indFreq] = newY;
        }

        // Update saved ports information
        this->ports.erase(this->ports.begin() + indPort); // Remove the port
        this->nPorts--; // Adjust number of ports
    }

    // Print the parasitics information
    void print() const
    {
        int numPort = getNPort();
        cout << " ------" << endl;
        cout << "  List of " << numPort << " ports:" << endl;
        for (size_t indi = 0; indi < numPort; indi++) // Handle each port name
        {
            (this->ports)[indi].print();
        }
        cout << "  Conductance Matrix (S):" << endl;
        for (size_t indi = 0; indi < this->matG.outerSize(); indi++)
        {
#ifdef EIGEN_SPARSE
            for (spMat::InnerIterator it(this->matG, indi); it; ++it)
            {
                cout << "   row " << setw(4) << it.row() + 1 << ", column " << setw(4) << it.col() + 1 << ", value " << it.value() << endl;
            }
#else
            for (size_t indj = 0; indj < this->matG.innerSize(); indj++)
            {
                cout << "   row " << setw(4) << indi + 1 << ", column " << setw(4) << indj + 1 << ", value " << this->matG(indi, indj) << endl;
            }
#endif
        }
        cout << "  Capacitance Matrix (F):" << endl;
        for (size_t indi = 0; indi < this->matC.outerSize(); indi++)
        {
#ifdef EIGEN_SPARSE
            for (spMat::InnerIterator it(this->matC, indi); it; ++it)
            {
                cout << "   row " << setw(4) << it.row() + 1 << ", column " << setw(4) << it.col() + 1 << ", value " << it.value() << endl;
            }
#else
            for (size_t indj = 0; indj < this->matC.innerSize(); indj++)
            {
                cout << "   row " << setw(4) << indi + 1 << ", column " << setw(4) << indj + 1 << ", value " << this->matC(indi, indj) << endl;
            }
#endif
        }
        if (this->freqs.size() != 1)
        {
            cout << " Using " << this->param << "-parameters for the " << this->freqs.size() << " frequencies in the sweep" << endl;
        }
        else
        {
            cout << " Using " << this->param << "-parameters for the single frequency in the sweep" << endl;
        }
        cout << " ------" << endl;
    }

    // Translate parasitics to Spef struct
    spef::Spef toSPEF(std::string designName, double saveThresh)
    {
        // Initialize variables
        spef::Spef para;                                                              // Spef struct for parasitics
        char timeStr[80];                                                             // Character array to hold formatted time
        time_t rawtime;                                                               // Timer variable
        strftime(timeStr, sizeof(timeStr), "%d-%m-%Y %H:%M:%S", localtime(&rawtime)); // Use local timezone to format string
        std::string time(timeStr);                                                    // Formatted time to string parametrized constructor
        size_t numPort = this->getNPort();                                            // Number of ports

        // Populate Spef struct header fields
        para.standard = "\"IEEE 1481-1998\"";
        para.design_name = "\"" + designName + "\"";
        para.date = "\"" + time + "\"";
        para.vendor = "\"DARPA ERI Contributors\"";
        para.program = "\"SPEF Writer from DARPA ERI\"";
        para.version = "\"1.0\"";
        para.design_flow = "\"NETLIST_TYPE_VERILOG\"";
        para.divider = "/";
        para.delimiter = ":";
        para.bus_delimiter = "[ ]";
        para.time_unit = "1 S";
        para.capacitance_unit = "1 F";
        para.resistance_unit = "1 OHM";
        para.inductance_unit = "1 H";

        // Populate Spef struct name map fields and ports vector
        for (size_t indi = 0; indi < numPort; indi++)
        {
            para.name_map.emplace(indi + 1, (this->ports)[indi].getPortName()); // Create name map for each port
            para.ports.emplace_back("*" + to_string(indi + 1));                 // Instantiate and push new port entry by name map
            switch ((this->ports)[indi].getPortDir())                           // Assign port direction
            {
            case 'O':
                para.ports.back().direction = spef::ConnectionDirection::OUTPUT;
                break;
            case 'I':
                para.ports.back().direction = spef::ConnectionDirection::INPUT;
                break;
            case 'B':
                para.ports.back().direction = spef::ConnectionDirection::INOUT;
                break;
            default:
                para.ports.back().direction = spef::ConnectionDirection::INOUT; // Treat as bidirectional if direction unclear
            }
        }

        // Symmetrize matrices if needed so that network is reciprocal
        this->makeCSym();
        this->makeGSym();

        // Populate Spef struct nets vector (single net)
        double capTot = this->getCTotal(); // Find total capacitance represented in matrix (F)
        double condTot = this->getGTotal();
#ifdef EIGEN_SPARSE
        (this->matC).prune(myPruneFunctor(saveThresh * capTot)); // Prune away nonzeros sufficiently smaller than threshold
        (this->matG).prune(myPruneFunctor(saveThresh * condTot));
#endif
        para.nets.emplace_back(spef::Net());
        para.nets.back().name = "all";
        para.nets.back().lcap = capTot;

        for (size_t indi = 0; indi < numPort; indi++)
        {
            para.nets.back().connections.emplace_back(spef::Connection());
            para.nets.back().connections.back().name = (this->ports)[indi].getPortName();
            para.nets.back().connections.back().type = spef::ConnectionType::EXTERNAL;    // All ports are external connections, not internal to cells
            para.nets.back().connections.back().direction = (para.ports[indi]).direction; // Same as port direction
#ifdef EIGEN_SPARSE
            for (spMat::InnerIterator it(this->matC, indi); it; ++it)
            {
                if ((it.row() == it.col()) && (abs(this->getCNodeGround(indi)) >= saveThresh * capTot)) // Diagonal element, so should be sufficient network admittance to ground
                {
                    para.nets.back().caps.emplace_back(forward_as_tuple((para.ports)[indi].name, "", this->getCNodeGround(indi)));
                }
                else if (it.col() > it.row()) // Upper triangular element, so negate node-to-node network admittance
                {
                    para.nets.back().caps.emplace_back(forward_as_tuple((para.ports)[indi].name, (para.ports)[it.col()].name, -it.value()));
                } // Skip lower triangular element, which is repeated due to symmetry
            }
            for (spMat::InnerIterator it(this->matG, indi); it; ++it)
            {
                if ((it.row() == it.col()) && (abs(this->getGNodeGround(indi)) >= saveThresh * condTot)) // Diagonal element, so should be sufficient network admittance to ground
                {
                    para.nets.back().ress.emplace_back(forward_as_tuple((para.ports)[indi].name, "", abs(1.0 / this->getGNodeGround(indi))));
                }
                else if (it.col() > it.row()) // Upper triangular element, so negate node-to-node network admittance
                {
                    para.nets.back().ress.emplace_back(forward_as_tuple((para.ports)[indi].name, (para.ports)[it.col()].name, abs(-1.0 / it.value())));
                } // Skip lower triangular element, which is repeated due to symmetry
            }
#else
            for (size_t indj = 0; indj < numPort; indj++)
            {
                if ((indi == indj) && (abs(this->getCNodeGround(indi)) >= saveThresh * capTot)) // Diagonal element, so should be sufficient network admittance to ground
                {
                    para.nets.back().caps.emplace_back(forward_as_tuple((para.ports)[indi].name, "", this->getCNodeGround(indi)));
                }
                else if ((indj > indi) && (abs(-(this->matC)(indi, indj)) >= saveThresh * capTot)) // Upper triangular element, so negate node-to-node network admittance
                {
                    para.nets.back().caps.emplace_back(forward_as_tuple((para.ports)[indi].name, (para.ports)[indj].name, -(this->matC)(indi, indj)));
                } // Skip lower triangular element, which is repeated due to symmetry
            }
            for (size_t indj = 0; indj < numPort; indj++)
            {
                if ((indi == indj) && (abs(this->getGNodeGround(indi)) >= saveThresh * condTot)) // Diagonal element, so should be sufficient network admittance to ground
                {
                    para.nets.back().ress.emplace_back(forward_as_tuple((para.ports)[indi].name, "", abs(1.0 / this->getGNodeGround(indi))));
                }
                else if ((indj > indi) && (abs(-1.0 / (this->matG(indi, indj))) >= saveThresh * condTot)) // Upper triangular element, so negate node-to-node network admittance
                {
                    para.nets.back().ress.emplace_back(forward_as_tuple((para.ports)[indi].name, (para.ports)[indj].name, abs(-1.0 / (this->matG(indi, indj)))));
                } // Skip lower triangular element, which is repeated due to symmetry
            }
#endif
        }

        // Return the Spef struct
        return para;
    }

    // Translate parasitics to Xyce (SPICE) subcircuit
    bool toXyce(std::string outXyceFile, std::string designName, double saveThresh)
    {
        // Initialize variables
        char timeStr[80];                                                             // Character array to hold formatted time
        time_t rawtime;                                                               // Timer variable
        strftime(timeStr, sizeof(timeStr), "%d-%m-%Y %H:%M:%S", localtime(&rawtime)); // Use local timezone to format string
        std::string time(timeStr);                                                    // Formatted time to string parametrized constructor
        size_t numPort = this->getNPort();                                            // Number of ports

        // Attempt to open file
        ofstream xyceFile(outXyceFile.c_str());
        if (xyceFile.is_open())
        {
            // Write SPICE subcircuit header
            xyceFile << "* File: " << outXyceFile << endl;
            xyceFile << "* Design: " << designName << endl;
            xyceFile << "* Vendor: DARPA ERI Contributors" << endl;
            xyceFile << "* Program: Xyce Writer from DARPA ERI" << endl;
            xyceFile << "* Author: Purdue University" << endl;
            xyceFile << "* Date: " << time << endl;
            xyceFile << endl;

            // Write the subcircuit definition line
            xyceFile << ".subckt " << designName; // Start subcircuit definition line
            for (size_t indi = 0; indi < numPort; indi++)
            {
                xyceFile << " " << (this->ports)[indi].getPortName();
            }
            xyceFile << endl; // End the subcircuit definition line

            // Symmetrize matrices if needed so that network is reciprocal
            this->makeCSym();
            this->makeGSym();

            // Write each circuit element
            double capTot = this->getCTotal();
            double condTot = this->getGTotal();
#ifdef EIGEN_SPARSE
            (this->matC).prune(myPruneFunctor(saveThresh * capTot)); // Prune away nonzeros sufficiently smaller than threshold
            (this->matG).prune(myPruneFunctor(saveThresh * condTot));
#endif
            int numCap = 1; // Running number of capacitors
            int numInd = 1; // Running number of inductors
            int numRes = 1; // Running number of resistors

            for (size_t indi = 0; indi < numPort; indi++) // Uses y-parameter storage in matC, matG
            {
#ifdef EIGEN_SPARSE
                for (spMat::InnerIterator it(this->matC, indi); it; ++it)
                {
                    if ((it.row() == it.col()) && (abs(this->getCNodeGround(indi)) >= saveThresh * capTot)) // Diagonal element
                    {
                        double Cng = this->getCNodeGround(indi);
                        if (Cng > 0.0)
                        {
                            xyceFile << "C" << numCap++ << " " << (this->ports)[indi].getPortName() << " 0 " << Cng << endl;
                        }
                        else
                        {
                            xyceFile << "L" << numInd++ << " " << (this->ports)[indi].getPortName() << " 0 " << -1.0 / (Cng * pow(2. * M_PI * this->freqs[0], 2.)) << endl; // Capacitor is really an inductor

                        }
                    }
                    else if (it.col() > it.row()) // Upper triangular element
                    {
                        double Cnn = -1.0 * it.value();
                        if (Cnn > 0.0)
                        {
                            xyceFile << "C" << numCap++ << " " << (this->ports)[indi].getPortName() << " " << (this->ports)[it.col()].getPortName() << " " << Cnn << endl;
                        }
                        else
                        {
                            xyceFile << "L" << numInd++ << " " << (this->ports)[indi].getPortName() << " " << (this->ports)[it.col()].getPortName() << " " << -1.0 / (Cng * pow(2. * M_PI * this->freqs[0], 2.)) /  << endl; // Capacitor is really an inductor
                        }
                    }
                }
                for (spMat::InnerIterator it(this->matG, indi); it; ++it)
                {
                    if ((it.row() == it.col()) && (abs(this->getGNodeGround(indi)) >= saveThresh * condTot)) // Diagonal element
                    {
                        xyceFile << "R" << numRes++ << " " << (this->ports)[indi].getPortName() << " 0 " << abs(1.0 / this->getGNodeGround(indi)) << endl;
                    }
                    else if (it.col() > it.row()) // Upper triangular element
                    {
                        xyceFile << "R" << numRes++ << " " << (this->ports)[indi].getPortName() << " " << (this->ports)[it.col()].getPortName() << " " << abs(-1.0 / it.value()) << endl;
                    }
                }
#else
                for (size_t indj = 0; indj < numPort; indj++)
                {
                    if ((indi == indj) && (abs(this->getCNodeGround(indi)) >= saveThresh * capTot)) // Diagonal element
                    {
                        double Cng = this->getCNodeGround(indi);
                        if (Cng > 0.0)
                        {
                            xyceFile << "C" << numCap++ << " " << (this->ports)[indi].getPortName() << " 0 " << Cng << endl;
                        }
                        else
                        {
                            xyceFile << "L" << numInd++ << " " << (this->ports)[indi].getPortName() << " 0 " << -1.0 / (Cng * pow(2. * M_PI * this->freqs[0], 2.)) << endl; // Capacitor is really an inductor
                        }
                    }
                    else if ((indj > indi) && (abs(-(this->matC)(indi, indj)) >= saveThresh * capTot)) // Upper triangular element
                    {
                        double Cnn = -1.0 * this->matC(indi, indj);
                        if (Cnn > 0.0)
                        {
                            xyceFile << "C" << numCap++ << " " << (this->ports)[indi].getPortName() << " " << (this->ports)[indj].getPortName() << " " << Cnn << endl;
                        }
                        else
                        {
                            xyceFile << "L" << numInd++ << " " << (this->ports)[indi].getPortName() << " " << (this->ports)[indj].getPortName() << " " << -1.0 / (Cnn * pow(2. * M_PI * this->freqs[0], 2.)) << endl; // Capacitor is really an inductor
                        }
                    }
                }
                for (size_t indj = 0; indj < numPort; indj++)
                {
                    if ((indi == indj) && (abs(this->getGNodeGround(indi)) >= saveThresh * condTot)) // Diagonal element
                    {
                        xyceFile << "R" << numRes++ << " " << (this->ports)[indi].getPortName() << " 0 " << abs(1.0 / this->getGNodeGround(indi)) << endl;
                    }
                    else if ((indj > indi) && (abs(-1.0 / (this->matG(indi, indj))) >= saveThresh * condTot)) // Upper triangular element
                    {
                        xyceFile << "R" << numRes++ << " " << (this->ports)[indi].getPortName() << " " << (this->ports)[indj].getPortName() << " " << abs(-1.0 / this->matG(indi, indj)) << endl;
                    }
                }
#endif
            }

            // Write the end of the subcircuit
            xyceFile << ".ends" << endl;

            // Close file
            xyceFile.close();
            return true;
        }
        else
        {
            // File could not be opened
            return false;
        }
    }

    // Destructor
    ~Parasitics()
    {
        vector<Port> ports = {};
        spMat emptMat;
        this->nPorts = 0;
        this->ports = ports;
        this->matG = emptMat;
        this->matC = emptMat;
    }
};

struct SolverDataBase
{
  private:
    std::string designName; // Name of design
    SimSettings settings;   // Simulation settings
    vector<Layer> layers;   // Layer stack-up information
    Waveforms wf;           // Waveforms
    Parasitics para;        // Parasitics
    std::string outSPEF;    // SPEF output file name
    std::string outXyce;    // Xyce (SPICE) output file name
    std::string outCITI;    // CITIfile output name
    std::string outTstone;  // Touchstone output file name
  public:
    // Default constructor
    SolverDataBase()
    {
        SimSettings settings;
        vector<Layer> layers;
        Waveforms wf;
        Parasitics para;
        this->designName = "";
        this->settings = settings;
        this->layers = layers;
        this->wf = wf;
        this->para = para;
        this->outSPEF = "";
        this->outXyce = "";
        this->outCITI = "";
        this->outTstone = "";
    }

    // Parametrized constructor
    SolverDataBase(std::string designName, Waveforms wf, Parasitics para)
    {
        SimSettings settings;
        vector<Layer> layers;
        this->designName = designName;
        this->settings = settings;
        this->layers = layers;
        this->wf = wf;
        this->para = para;
        this->outSPEF = "";
        this->outXyce = "";
        this->outCITI = "";
        this->outTstone = "";
    }

    // Get name of design
    std::string getDesignName() const
    {
        return this->designName;
    }

    // Get simulation settings
    SimSettings getSimSettings() const
    {
        return this->settings;
    }

    // Get number of layers
    size_t getNumLayer() const
    {
        return (this->layers).size();
    }

    // Get vector of layer information
    vector<Layer> getLayers() const
    {
        return this->layers;
    }

    // Get vector of information for valid, physical layers
    vector<Layer> getValidLayers() const
    {
        vector<Layer> validLayers;
        for (size_t indLayer = 0; indLayer < this->getNumLayer(); indLayer++)
        {
            if (this->layers[indLayer].isValid())
            {
                validLayers.push_back(this->layers[indLayer]);
            }
        }
        return validLayers;
    }

    // Get waveforms
    Waveforms getWaveforms() const
    {
        return this->wf;
    }

    // Get parasitics
    Parasitics getParasitics() const
    {
        return this->para;
    }

    // Get SPEF output file name
    std::string getOutSPEF() const
    {
        return this->outSPEF;
    }

    // Get Xyce (SPICE) output file name
    std::string getOutXyce() const
    {
        return this->outXyce;
    }

    // Get CITIfile output name
    std::string getOutCITI() const
    {
        return this->outCITI;
    }

    // Get Touchstone output file name
    std::string getOutTouchstone() const
    {
        return this->outTstone;
    }

    // Set name of design
    void setDesignName(std::string designName)
    {
        this->designName = designName;
    }

    // Set simulation settings
    void setSimSettings(SimSettings newSettings)
    {
        this->settings = newSettings;
    }

    // Set layer stackup
    void setLayers(vector<Layer> layers)
    {
        this->layers = layers;
    }

    // Set waveforms
    void setWaveforms(Waveforms wf)
    {
        this->wf = wf;
    }

    // Set parasitics
    void setParasitics(Parasitics para)
    {
        this->para = para;
    }

    // Set SPEF output file name
    void setOutSPEF(std::string fileName)
    {
        this->outSPEF = fileName;
    }

    // Set Xyce (SPICE) output file name
    void setOutXyce(std::string fileName)
    {
        this->outXyce = fileName;
    }

    // Set CITIfile output name
    void setOutCITI(std::string fileName)
    {
        this->outCITI = fileName;
    }

    // Set Touchstone output file name
    void setOutTouchstone(std::string fileName)
    {
        this->outTstone = fileName;
    }

    // Find index of layer by name
    // Returns index past number of layers if not found
    size_t locateLayerName(std::string name) const
    {
        size_t indLayer;
        for (indLayer = 0; indLayer < (this->layers).size(); indLayer++)
        {
            if (name.compare((this->layers[indLayer]).getLayerName()) == 0)
            {
                return indLayer;
            }
        }
        return indLayer;
    }

    // Find index of layer by GDSII number
    // Returns index past number of layers if not found
    size_t locateLayerGDSII(int gdsiiNum) const
    {
        size_t indLayer;
        for (indLayer = 0; indLayer < (this->layers).size(); indLayer++)
        {
            if ((this->layers[indLayer]).getGDSIINum() == gdsiiNum)
            {
                return indLayer;
            }
        }
        return indLayer;
    }

    // Find index of layer by starting z-coordinate
    // Returns index past number of layers if not found
    size_t locateLayerZStart(double zStart) const
    {
        size_t indLayer;
        for (indLayer = 0; indLayer < (this->layers).size(); indLayer++)
        {
            // See if layer start coordinate is within 1% of the height of that layer
            if (abs((this->layers[indLayer]).getZStart() - zStart) < 0.01 * (this->layers[indLayer]).getZHeight())
            {
                return indLayer;
            }
        }
        return indLayer;
    }

    // Return a layer
    Layer getLayer(size_t indLayer) const
    {
        return (this->layers)[indLayer];
    }

    // Return all layer names
    vector<std::string> findLayerNames() const
    {
        vector<std::string> names;
        for (size_t indi = 0; indi < (this->layers).size(); indi++)
        {
            names.push_back(((this->layers)[indi]).getLayerName());
        }
        return names;
    }

    // Find layers that solver should ignore
    vector<int> findLayerIgnore() const
    {
        vector<int> gdsiiLayerIgnore;
        for (size_t indLayer = 0; indLayer < this->getNumLayer(); indLayer++)
        {
            if (!this->getLayer(indLayer).isValid())
            {
                gdsiiLayerIgnore.push_back(this->getLayer(indLayer).getGDSIINum()); // Ignore layers of zero height
            }
        }
        return gdsiiLayerIgnore;
    }

    // Read outline (or unmarked dimension) Gerber file (extension must be included)
    vector<double> readGerberOutline(std::string outlineGerberName)
    {
        // Attempt to open Gerber file with outline
        ifstream outlineFile(outlineGerberName.c_str());
        if (outlineFile.is_open())
        {
            // Build single geometric cell from limboint.h to store information (forget about timestamps)
            GeoCell cellGerb;
            cellGerb.cellName = "outline";

            // Build ASCII database from limboint.h (forget about timestamps)
            AsciiDataBase adbGerb;
            adbGerb.setFileName(outlineGerberName.substr(0, outlineGerberName.find_last_of(".")) + ".gds");

            // Gerber file is barely readable line-by-line
            std::string fileLine;
            getline(outlineFile, fileLine);

            // Skip all comment lines (starting with G-code "G04" in RS-274X)
            while (fileLine.compare(0, 3, "G04") == 0)
            {
                getline(outlineFile, fileLine);
            }

            // Initialize file-scope variables
            int intPartX = 2; // Should be <= 6
            int fracPartX = 4; // Should be 4, 5, or 6
            int intPartY = 2; // Should be <= 6
            int fracPartY = 4; // Should be 4, 5, or 6
            int graphicsMode = 0; // Active modes are "G01" (linear interpolation), "G02" (CW circular interp.), "G03" (CCW circular interp.), "G04" (comment), "G36"/"G37" (set region mode on/off), "G74"/"G75" (set single/multi quadrant)
            bool regionMode = false; // Is region mode on?
            bool singleQuadMode = true; // true = single quadrant mode (angle between 0 and pi/2 rad), false = multi quadrant mode (angle greater than 0 and up to 2*pi rad)
            vector<double> currentPt = { 0., 0. }; // Coordinates of current point
            vector<Aperture> customAper = {}; // Custom apertures
            Aperture currentAper = Aperture(); // Current aperture in use

            // Read rest of file line-by-line with very rough parser
            while (!outlineFile.eof())
            {
                // Handle format specification (starting with configuration parameter "%FS" in RS-274X)
                if (fileLine.compare(0, 3, "%FS") == 0)
                {
                    // Find format specification delimiters for each coordinate
                    size_t indXFormat = fileLine.find("X");
                    size_t indYFormat = fileLine.find("Y");

                    // Save format specification information to variables
                    intPartX = stoi(fileLine.substr(indXFormat + 1, 1)); // First character after coordinate specifier is integer part of number representation
                    fracPartX = stoi(fileLine.substr(indXFormat + 2, 1)); // Second character after coordinate specifier is fractional part of number representation
                    intPartY = stoi(fileLine.substr(indYFormat + 1, 1));
                    fracPartY = stoi(fileLine.substr(indYFormat + 2, 1));
                }
                // Handle units (starting with configuration parameter "%MO" in RS-274X)
                else if (fileLine.compare(0, 3, "%MO") == 0)
                {
                    double multSI = 1.0;

                    // Units are in inches
                    if (fileLine.compare(3, 2, "IN") == 0)
                    {
                        multSI = 0.0254;
                    }
                    // Units are in millimeters
                    else if (fileLine.compare(3, 2, "MM") == 0)
                    {
                        multSI = 1e-3;
                    }

                    // Propagate units information to ASCII database now
                    adbGerb.setdbUserUnits(1.);
                    adbGerb.setdbUnits(multSI);
                }
                // Check image polarity (starting with configuration parameter "%IP" in RS-274X, affects nothing at present)
                else if (fileLine.compare(0, 3, "%IP") == 0)
                {
                    bool isPosPol = true;
                    if (fileLine.compare(3, 3, "POS") == 0)
                    {
                        isPosPol = true;
                    }
                    else if (fileLine.compare(3, 3, "NEG") == 0)
                    {
                        isPosPol = false;
                    }
                }
                // Define an aperture (starting with D-code "%AD" in RS-274X) without allowing aperture macros
                else if (fileLine.compare(0, 3, "%AD") == 0)
                {
                    // Find delimiters
                    size_t indDNum = fileLine.find("D", 3); // D-code operation delimiter
                    size_t indCirc = fileLine.find("C,"); // Standard aperture template for circle
                    size_t indRect = fileLine.find("R,"); // Standard aperture template for rectangle
                    size_t indStad = fileLine.find("O,"); // Standard aperture template for stadium (obround)
                    size_t indPoly = fileLine.find("P,"); // Standard aperture template for regular polygon
                    size_t indGraphicClose = fileLine.find("*%");

                    // Follow standard aperture templates
                    int aperNum = 0;
                    if (indCirc != string::npos)
                    {
                        // Find D-code for aperture number
                        aperNum = stoi(fileLine.substr(indDNum + 1, indCirc - indDNum - 1));

                        // Check for hole delimiter in standard aperture template for circle
                        double diameter = 0.0; // Outer diameter of circle (m)
                        double holeDia = 0.0; // Diameter of center hole (m)
                        size_t indHole = fileLine.find("X", indCirc);

                        // Create aperture for circle without a hole
                        if (indHole == string::npos)
                        {
                            diameter = stod(fileLine.substr(indCirc + 2, indGraphicClose - indCirc - 2)) * adbGerb.getdbUnits();
                        }
                        // Create aperture for circle with a hole (annulus)
                        else
                        {
                            diameter = stod(fileLine.substr(indCirc + 2, indHole - indCirc - 2)) * adbGerb.getdbUnits();
                            holeDia = stod(fileLine.substr(indHole + 2, indGraphicClose - indHole - 2)) * adbGerb.getdbUnits();
                        }

                        // Add defined aperture to custom apertures vector
                        customAper.emplace_back(Aperture(aperNum, diameter, holeDia));
                    }
                    else if (indRect != string::npos)
                    {
                        // Find D-code for aperture number
                        aperNum = stoi(fileLine.substr(indDNum + 1, indRect - indDNum - 1));

                        // Check for y-dimension size and hole delimiters in standard aperture template for rectangle
                        double xSize = 0.0; // Width in x-direction (m)
                        double ySize = 0.0; // Lenght in y-direction (m)
                        double holeDia = 0.0; // Diameter of center hole (m)
                        size_t indYSize = fileLine.find("X", indRect); // Confusingly, the y-size comes after the x-size and letter "X"
                        size_t indHole = fileLine.find("X", indYSize);
                        xSize = stod(fileLine.substr(indRect + 2, indYSize - indRect - 2)) * adbGerb.getdbUnits();

                        // Create aperture for rectangle without a hole
                        if (indHole == string::npos)
                        {
                            ySize = stod(fileLine.substr(indYSize + 1, indGraphicClose - indYSize - 1)) * adbGerb.getdbUnits();
                        }
                        // Create aperture for rectangle with a hole
                        else
                        {
                            ySize = stod(fileLine.substr(indYSize + 1, indHole - indYSize - 1)) * adbGerb.getdbUnits();
                            holeDia = stod(fileLine.substr(indHole + 1, indGraphicClose - indHole - 1)) * adbGerb.getdbUnits();
                        }

                        // Add defined aperture to custom apertures vector
                        customAper.emplace_back(Aperture(aperNum, 'R', xSize, ySize, holeDia));
                    }
                    else if (indStad != string::npos)
                    {
                        // Find D-code for aperture number
                        aperNum = stoi(fileLine.substr(indDNum + 1, indStad - indDNum - 1));

                        // Check for y-dimension enclosing size and hole delimiters in standard aperture template for stadium (obround)
                        double xSize = 0.0; // Enclosing box size in x-direction (m)
                        double ySize = 0.0; // Enclosing box size in y-direction (m)
                        double holeDia = 0.0; // Diameter of center hole (m)
                        size_t indYSize = fileLine.find("X", indStad); // Confusingly, the y-dimension enclosing box size comes after the x-dimension enclosing box size and letter "X"
                        size_t indHole = fileLine.find("X", indYSize);
                        xSize = stod(fileLine.substr(indStad + 2, indYSize - indStad - 2)) * adbGerb.getdbUnits();

                        // Create aperture for stadium without a hole
                        if (indHole == string::npos)
                        {
                            ySize = stod(fileLine.substr(indYSize + 1, indGraphicClose - indYSize - 1)) * adbGerb.getdbUnits();
                        }
                        // Create aperture for stadium with a hole
                        else
                        {
                            ySize = stod(fileLine.substr(indYSize + 1, indHole - indYSize - 1)) * adbGerb.getdbUnits();
                            holeDia = stod(fileLine.substr(indHole + 1, indGraphicClose - indHole - 1)) * adbGerb.getdbUnits();
                        }

                        // Add defined aperture to custom apertures vector
                        customAper.emplace_back(Aperture(aperNum, 'O', xSize, ySize, holeDia));
                    }
                    else if (indPoly != string::npos)
                    {
                        // Find D-code for aperture number
                        aperNum = stoi(fileLine.substr(indDNum + 1, indPoly - indDNum - 1));

                        // Check for y-dimension enclosing size and hole delimiters in standard aperture template for regular polygon
                        double circumDia = 0.0; // Circumdiameter, the diameter of the circle circumscribing the vertices
                        int nVert = 3; // Number of vertices between 3 and 12
                        double rotAngle = 0.0; // Rotation angle (rad)
                        double holeDia = 0.0;  // Diameter of center hole (m)
                        size_t indVert = fileLine.find("X", indPoly);
                        size_t indRot = fileLine.find("X", indVert);
                        size_t indHole = fileLine.find("X", indRot); // Should return string::npos if indRot could not be found previously
                        circumDia = stod(fileLine.substr(indPoly + 2, indVert - indPoly - 2)) * adbGerb.getdbUnits();

                        // Create aperture for regular polygon without rotation
                        if (indRot == string::npos)
                        {
                            nVert = stoi(fileLine.substr(indVert + 1, indGraphicClose - indVert - 1));
                        }
                        // Create aperture for regular polygon with rotation
                        else
                        {
                            nVert = stoi(fileLine.substr(indVert + 1, indRot - indVert - 1));

                            // Rotated regular polygon aperture lacks a hole
                            if (indHole == string::npos)
                            {
                                rotAngle = stod(fileLine.substr(indRot + 1, indGraphicClose - indRot - 1)) * M_PI / 180.;
                            }
                            // Rotated regular polygon aperture has a hole
                            else
                            {
                                rotAngle = stod(fileLine.substr(indRot + 1, indHole - indRot - 1)) * M_PI / 180.;
                                holeDia = stod(fileLine.substr(indHole + 1, indGraphicClose - indHole - 1)) * adbGerb.getdbUnits();
                            }
                        }

                        // Add defined aperture to custom apertures vector
                        customAper.emplace_back(Aperture(aperNum, 'P', circumDia, holeDia, nVert, rotAngle));
                    }
                    else
                    {
                        // Find where macro name starts
                        size_t indMacroName = indDNum + 1;
                        while (isdigit(fileLine[indMacroName]))
                        {
                            indMacroName++;
                        }

                        // Find D-code for aperture number
                        aperNum = stoi(fileLine.substr(indDNum + 1, indMacroName - indDNum - 1));

                        // Add ethereal circular aperture to custom apertures vector to hold the place of aperture macro
                        customAper.emplace_back(Aperture(aperNum, 0.0, 0.0));
                    }
                }
                // Set an aperture previously defined (starting with D-code "Dnn" in RS-274X, where "nn" >= 10)
                else if (fileLine.compare(0, 1, "D") == 0)
                {
                    // Find D-code of aperture
                    size_t indGraphicClose = fileLine.find("*");
                    int aperNum = stoi(fileLine.substr(1, indGraphicClose - 1));

                    // Look up the aperture among already defined apertures
                    for (size_t indAper = 0; indAper < customAper.size(); indAper++)
                    {
                        if (aperNum == customAper[indAper].getAperNum())
                        {
                            // Switch to aperture
                            currentAper = customAper[indAper];
                        }
                    }
                }
                // Switch graphics mode to linear interpolation (starting with G-code "G01" in RS-274X)
                else if (fileLine.compare(0, 3, "G01") == 0)
                {
                    graphicsMode = 1;

                    // See if any additional information on this line
                    size_t indGraphicClose = fileLine.find("*");
                    if (indGraphicClose > 3)
                    {
                        // Find delimiters
                        size_t indXEnd = fileLine.find("X"); // Ending x-coordinate delimiter
                        size_t indYEnd = fileLine.find("Y"); // Ending y-coordinate delimiter
                        size_t indDCOp = fileLine.find("D"); // D-code operation delimiter

                        // Extract numbers
                        double xEnd = currentPt[0]; // Initialize ending point to current point
                        double yEnd = currentPt[1];
                        if ((indXEnd != string::npos) && (indYEnd != string::npos))
                        {
                            xEnd = stod(fileLine.substr(indXEnd + 1, indYEnd - indXEnd - 1)) / pow(10.0, fracPartX) * adbGerb.getdbUnits(); // Length is index difference minus included characters with fractional part correction
                        }
                        else if ((indXEnd != string::npos) && (indYEnd == string::npos))
                        {
                            xEnd = stod(fileLine.substr(indXEnd + 1, indDCOp - indXEnd - 1)) / pow(10.0, fracPartX) * adbGerb.getdbUnits();
                        }
                        if (indYEnd != string::npos)
                        {
                            yEnd = stod(fileLine.substr(indYEnd + 1, indDCOp - indYEnd - 1)) / pow(10.0, fracPartY) * adbGerb.getdbUnits();
                        }
                        int DCOp = stoi(fileLine.substr(indDCOp + 1, indGraphicClose - indDCOp - 1));

                        // Perform D-code operation
                        switch (DCOp)
                        {
                        case 01:
                        {
                            // Interpolate operation
                            int pathType; // pathType: 0 = square ends at vertices, 1 = round ends, 2 = square ends overshoot vertices by half width
                            if ((currentAper.getStanTemp() == 'C') || (currentAper.getStanTemp() == 'O'))
                            {
                                pathType = 1;
                            }
                            else
                            {
                                pathType = 2;
                            }
                            double width = currentAper.getCircumDia(); // Worst case scenario used

                            // Push new path to the geometric cell
                            cellGerb.paths.emplace_back(path({ currentPt[0], currentPt[1], xEnd, yEnd }, 1, {}, pathType, width));

                            // Update current point
                            currentPt = { xEnd, yEnd };
                            break;
                        }
                        case 02:
                            // Move operation

                            // Update current point
                            currentPt = { xEnd, yEnd };
                            break;
                        case 03:
                            // Flash operation (supposedly not allowed in region mode)

                            // Update current point
                            currentPt = { xEnd, yEnd };

                            // Push polygon boundary of aperture image to geometric cell
                            cellGerb.boundaries.emplace_back(currentAper.drawAsBound(xEnd, yEnd));
                            break;
                        }
                    }
                }
                // Switch graphics mode to clockwise circular interpolation (starting with G-code "G02" in RS-274X)
                else if (fileLine.compare(0, 3, "G02") == 0)
                {
                    graphicsMode = 2;

                    // See if any additional information on this line
                    size_t indGraphicClose = fileLine.find("*");
                    if (indGraphicClose > 3)
                    {
                        // Find delimiters
                        size_t indXEnd = fileLine.find("X"); // Ending x-coordinate delimiter
                        size_t indYEnd = fileLine.find("Y"); // Ending y-coordinate delimiter
                        size_t indIOff = fileLine.find("I"); // Arc offset distance in x-direction
                        size_t indJOff = fileLine.find("J"); // Arc offset distance in y-direction
                        size_t indDCOp = fileLine.find("D"); // D-code operation delimiter

                        // Extract numbers
                        double xEnd = currentPt[0]; // Initialize ending point to current point
                        double yEnd = currentPt[1];
                        double iOff = 0.; // Initialize as zero offset from starting point to arc center (nonnegative for single quadrant, signed for multi quadrant)
                        double jOff = 0.;
                        if ((indXEnd != string::npos) && (indYEnd != string::npos))
                        {
                            xEnd = stod(fileLine.substr(indXEnd + 1, indYEnd - indXEnd - 1)) / pow(10.0, fracPartX) * adbGerb.getdbUnits(); // Length is index difference minus included characters with fractional part correction
                        }
                        else if ((indXEnd != string::npos) && (indYEnd == string::npos) && (indIOff != string::npos))
                        {
                            xEnd = stod(fileLine.substr(indXEnd + 1, indIOff - indXEnd - 1)) / pow(10.0, fracPartX) * adbGerb.getdbUnits();
                        }
                        else if ((indXEnd != string::npos) && (indYEnd == string::npos) && (indIOff == string::npos) && (indJOff != string::npos))
                        {
                            xEnd = stod(fileLine.substr(indXEnd + 1, indJOff - indXEnd - 1)) / pow(10.0, fracPartX) * adbGerb.getdbUnits();
                        }
                        else
                        {
                            xEnd = stod(fileLine.substr(indXEnd + 1, indDCOp - indXEnd - 1)) / pow(10.0, fracPartX) * adbGerb.getdbUnits();
                        }
                        if ((indYEnd != string::npos) && (indIOff != string::npos))
                        {
                            yEnd = stod(fileLine.substr(indYEnd + 1, indIOff - indYEnd - 1)) / pow(10.0, fracPartY) * adbGerb.getdbUnits();
                        }
                        else if ((indYEnd != string::npos) && (indIOff == string::npos) && (indJOff != string::npos))
                        {
                            yEnd = stod(fileLine.substr(indYEnd + 1, indJOff - indYEnd - 1)) / pow(10.0, fracPartY) * adbGerb.getdbUnits();
                        }
                        else
                        {
                            yEnd = stod(fileLine.substr(indYEnd + 1, indDCOp - indYEnd - 1)) / pow(10.0, fracPartY) * adbGerb.getdbUnits();
                        }
                        if ((indIOff != string::npos) && (indJOff != string::npos))
                        {
                            iOff = stod(fileLine.substr(indIOff + 1, indJOff - indIOff - 1)) / pow(10.0, fracPartX) * adbGerb.getdbUnits();
                        }
                        else
                        {
                            iOff = stod(fileLine.substr(indIOff + 1, indDCOp - indIOff - 1)) / pow(10.0, fracPartX) * adbGerb.getdbUnits();
                        }
                        if (indJOff != string::npos)
                        {
                            jOff = stod(fileLine.substr(indJOff + 1, indDCOp - indJOff - 1)) / pow(10.0, fracPartY) * adbGerb.getdbUnits();
                        }
                        int DCOp = stoi(fileLine.substr(indDCOp + 1, indGraphicClose - indDCOp - 1));

                        // Perform D-code operation
                        switch (DCOp)
                        {
                        case 01:
                        {
                            // Interpolate operation
                            int pathType; // pathType: 0 = square ends at vertices, 1 = round ends, 2 = square ends overshoot vertices by half width
                            if ((currentAper.getStanTemp() == 'C') || (currentAper.getStanTemp() == 'O'))
                            {
                                pathType = 1;
                            }
                            else
                            {
                                pathType = 2;
                            }
                            double width = currentAper.getCircumDia(); // Worst case scenario used

                            // Calculate center of circular arc, radius, and angle covered
                            double xStart = currentPt[0];
                            double yStart = currentPt[1];
                            double xCent = xStart; // Initialize to no offset from starting point
                            double yCent = yStart;
                            double arcRad = 0.0;
                            double startAngle = 0.0;
                            double endAngle = 0.0;
                            double arcAngle = 0.0;
                            if (singleQuadMode)
                            {
                                // Single quadrant mode has unsigned offsets to center in CW interpolation
                                if ((xEnd <= xStart) && (yEnd >= yStart))
                                {
                                    // Arc moves back and up
                                    xCent += iOff;
                                    yCent += jOff;
                                }
                                else if ((xEnd <= xStart) && (yEnd <= yStart))
                                {
                                    // Arc moves back and down
                                    xCent -= iOff;
                                    yCent += jOff;
                                }
                                else if ((xEnd >= xStart) && (yEnd <= yStart))
                                {
                                    // Arc moves forward and down
                                    xCent -= iOff;
                                    yCent -= jOff;
                                }
                                else if ((xEnd >= xStart) && (yEnd >= yStart))
                                {
                                    // Arc moves forward and up
                                    xCent += iOff;
                                    yCent -= jOff;
                                }
                                arcRad = 0.5 * (hypot(xStart - xCent, yStart - yCent) + hypot(xEnd - xCent, yEnd - yCent)); // Arithmetic mean because rounding errors prevent perfect circle
                                startAngle = atan2(yStart - yCent, xStart - xCent);
                                endAngle = atan2(yEnd - yCent, xEnd - xCent);
                                arcAngle = acos(((xStart - xCent) * (xEnd - xCent) + (yStart - yCent) * (yEnd - yCent)) / (hypot(xStart - xCent, yStart - yCent) * hypot(xEnd - xCent, yEnd - yCent))); // Dot product formula
                            }
                            else
                            {
                                // Multi quadrant mode has signed offsets to center
                                xCent += iOff;
                                yCent += jOff;
                                arcRad = 0.5 * (hypot(xStart - xCent, yStart - yCent) + hypot(xEnd - xCent, yEnd - yCent)); // Arithmetic mean because rounding errors prevent perfect circle
                                startAngle = atan2(yStart - yCent, xStart - xCent);
                                if (startAngle < 0)
                                {
                                    startAngle += 2.0 * M_PI; // Have to work entirely with angles between 0 and 2*pi
                                }
                                endAngle = atan2(yEnd - yCent, xEnd - xCent);
                                if (endAngle < 0)
                                {
                                    endAngle += 2.0 * M_PI; // Have to work entirely with angles between 0 and 2*pi
                                }
                                arcAngle = startAngle - endAngle; // Difference of angles CW
                            }

                            // Calculate points along arc with irregular 24-gon approximation
                            size_t nArcPt = ceil(arcAngle / (M_PI / 12.));
                            vector<double> paths; // Initialize vector of path coordinates
                            paths.push_back(xStart); // x-coordinate of current point
                            paths.push_back(yStart); // y-coordinate of current point
                            for (size_t indi = 1; indi < nArcPt; indi++)
                            {
                                paths.push_back(xCent + arcRad * cos(-2.0 * M_PI * indi / 24 + startAngle)); // x-coordinate CW
                                paths.push_back(yCent + arcRad * sin(-2.0 * M_PI * indi / 24 + startAngle)); // y-coordinate CW
                            }
                            paths.push_back(xEnd); // x-coordinate of end point
                            paths.push_back(yEnd); // y-coordinate of end point

                            // Push new path to the geometric cell
                            cellGerb.paths.emplace_back(path(paths, 1, {}, pathType, width));

                            // Update current point
                            currentPt = { xEnd, yEnd };
                            break;
                        }
                        case 02:
                            // Move operation

                            // Update current point
                            currentPt = { xEnd, yEnd };
                            break;
                        case 03:
                            // Flash operation (supposedly not allowed in region mode)

                            // Update current point
                            currentPt = { xEnd, yEnd };

                            // Push polygon boundary of aperture image to geometric cell
                            cellGerb.boundaries.emplace_back(currentAper.drawAsBound(xEnd, yEnd));
                            break;
                        }
                    }
                }
                // Switch graphics mode to counterclockwise circular interpolation (starting with G-code "G03" in RS-274X)
                else if (fileLine.compare(0, 3, "G03") == 0)
                {
                    graphicsMode = 3;

                    // See if any additional information on this line
                    size_t indGraphicClose = fileLine.find("*");
                    if (indGraphicClose > 3)
                    {
                        // Find delimiters
                        size_t indXEnd = fileLine.find("X"); // Ending x-coordinate delimiter
                        size_t indYEnd = fileLine.find("Y"); // Ending y-coordinate delimiter
                        size_t indIOff = fileLine.find("I"); // Arc offset distance in x-direction
                        size_t indJOff = fileLine.find("J"); // Arc offset distance in y-direction
                        size_t indDCOp = fileLine.find("D"); // D-code operation delimiter

                        // Extract numbers
                        double xEnd = currentPt[0]; // Initialize ending point to current point
                        double yEnd = currentPt[1];
                        double iOff = 0.; // Initialize as zero offset from starting point to arc center (nonnegative for single quadrant, signed for multi quadrant)
                        double jOff = 0.;
                        if ((indXEnd != string::npos) && (indYEnd != string::npos))
                        {
                            xEnd = stod(fileLine.substr(indXEnd + 1, indYEnd - indXEnd - 1)) / pow(10.0, fracPartX) * adbGerb.getdbUnits(); // Length is index difference minus included characters with fractional part correction
                        }
                        else if ((indXEnd != string::npos) && (indYEnd == string::npos) && (indIOff != string::npos))
                        {
                            xEnd = stod(fileLine.substr(indXEnd + 1, indIOff - indXEnd - 1)) / pow(10.0, fracPartX) * adbGerb.getdbUnits();
                        }
                        else if ((indXEnd != string::npos) && (indYEnd == string::npos) && (indIOff == string::npos) && (indJOff != string::npos))
                        {
                            xEnd = stod(fileLine.substr(indXEnd + 1, indJOff - indXEnd - 1)) / pow(10.0, fracPartX) * adbGerb.getdbUnits();
                        }
                        else
                        {
                            xEnd = stod(fileLine.substr(indXEnd + 1, indDCOp - indXEnd - 1)) / pow(10.0, fracPartX) * adbGerb.getdbUnits();
                        }
                        if ((indYEnd != string::npos) && (indIOff != string::npos))
                        {
                            yEnd = stod(fileLine.substr(indYEnd + 1, indIOff - indYEnd - 1)) / pow(10.0, fracPartY) * adbGerb.getdbUnits();
                        }
                        else if ((indYEnd != string::npos) && (indIOff == string::npos) && (indJOff != string::npos))
                        {
                            yEnd = stod(fileLine.substr(indYEnd + 1, indJOff - indYEnd - 1)) / pow(10.0, fracPartY) * adbGerb.getdbUnits();
                        }
                        else
                        {
                            yEnd = stod(fileLine.substr(indYEnd + 1, indDCOp - indYEnd - 1)) / pow(10.0, fracPartY) * adbGerb.getdbUnits();
                        }
                        if ((indIOff != string::npos) && (indJOff != string::npos))
                        {
                            iOff = stod(fileLine.substr(indIOff + 1, indJOff - indIOff - 1)) / pow(10.0, fracPartX) * adbGerb.getdbUnits();
                        }
                        else
                        {
                            iOff = stod(fileLine.substr(indIOff + 1, indDCOp - indIOff - 1)) / pow(10.0, fracPartX) * adbGerb.getdbUnits();
                        }
                        if (indJOff != string::npos)
                        {
                            jOff = stod(fileLine.substr(indJOff + 1, indDCOp - indJOff - 1)) / pow(10.0, fracPartY) * adbGerb.getdbUnits();
                        }
                        int DCOp = stoi(fileLine.substr(indDCOp + 1, indGraphicClose - indDCOp - 1));

                        // Perform D-code operation
                        switch (DCOp)
                        {
                        case 01:
                        {
                            // Interpolate operation
                            int pathType; // pathType: 0 = square ends at vertices, 1 = round ends, 2 = square ends overshoot vertices by half width
                            if ((currentAper.getStanTemp() == 'C') || (currentAper.getStanTemp() == 'O'))
                            {
                                pathType = 1;
                            }
                            else
                            {
                                pathType = 2;
                            }
                            double width = currentAper.getCircumDia(); // Worst case scenario used

                            // Calculate center of circular arc, radius, and angle covered
                            double xStart = currentPt[0];
                            double yStart = currentPt[1];
                            double xCent = xStart; // Initialize to no offset from starting point
                            double yCent = yStart;
                            double arcRad = 0.0;
                            double startAngle = 0.0;
                            double endAngle = 0.0;
                            double arcAngle = 0.0;
                            if (singleQuadMode)
                            {
                                // Single quadrant mode has unsigned offsets to center in CCW interpolation
                                if ((xEnd <= xStart) && (yEnd >= yStart))
                                {
                                    // Arc moves back and up
                                    xCent -= iOff;
                                    yCent -= jOff;
                                }
                                else if ((xEnd <= xStart) && (yEnd <= yStart))
                                {
                                    // Arc moves back and down
                                    xCent += iOff;
                                    yCent -= jOff;
                                }
                                else if ((xEnd >= xStart) && (yEnd <= yStart))
                                {
                                    // Arc moves forward and down
                                    xCent += iOff;
                                    yCent += jOff;
                                }
                                else if ((xEnd >= xStart) && (yEnd >= yStart))
                                {
                                    // Arc moves forward and up
                                    xCent -= iOff;
                                    yCent += jOff;
                                }
                                arcRad = 0.5 * (hypot(xStart - xCent, yStart - yCent) + hypot(xEnd - xCent, yEnd - yCent)); // Arithmetic mean because rounding errors prevent perfect circle
                                startAngle = atan2(yStart - yCent, xStart - xCent);
                                endAngle = atan2(yEnd - yCent, xEnd - xCent);
                                arcAngle = acos(((xStart - xCent) * (xEnd - xCent) + (yStart - yCent) * (yEnd - yCent)) / (hypot(xStart - xCent, yStart - yCent) * hypot(xEnd - xCent, yEnd - yCent))); // Dot product formula
                            }
                            else
                            {
                                // Multi quadrant mode has signed offsets to center
                                xCent += iOff;
                                yCent += jOff;
                                arcRad = 0.5 * (hypot(xStart - xCent, yStart - yCent) + hypot(xEnd - xCent, yEnd - yCent)); // Arithmetic mean because rounding errors prevent perfect circle
                                startAngle = atan2(yStart - yCent, xStart - xCent);
                                if (startAngle < 0)
                                {
                                    startAngle += 2.0 * M_PI; // Have to work entirely with angles between 0 and 2*pi
                                }
                                endAngle = atan2(yEnd - yCent, xEnd - xCent);
                                if (endAngle < 0)
                                {
                                    endAngle += 2.0 * M_PI; // Have to work entirely with angles between 0 and 2*pi
                                }
                                arcAngle = endAngle - startAngle; // Difference of angles CCW
                            }

                            // Calculate points along arc with irregular 24-gon approximation
                            size_t nArcPt = ceil(arcAngle / (M_PI / 12.));
                            vector<double> paths; // Initialize vector of path coordinates
                            paths.push_back(xStart); // x-coordinate of current point
                            paths.push_back(yStart); // y-coordinate of current point
                            //cout << "startPt: (" << xStart << ", " << yStart << ")" << endl;
                            //cout << " path points: ";
                            for (size_t indi = 1; indi < nArcPt; indi++)
                            {
                                paths.push_back(xCent + arcRad * cos(2.0 * M_PI * indi / 24 + startAngle)); // x-coordinate CCW
                                paths.push_back(yCent + arcRad * sin(2.0 * M_PI * indi / 24 + startAngle)); // y-coordinate CCW
                                //cout << "(" << xCent + arcRad * cos(2.0 * M_PI * indi / 24 + startAngle) << ", " << yCent + arcRad * sin(2.0 * M_PI * indi / 24 + startAngle) << ") ";
                            }
                            //cout << endl;
                            paths.push_back(xEnd); // x-coordinate of end point
                            paths.push_back(yEnd); // y-coordinate of end point

                            // Push new path to the geometric cell
                            cellGerb.paths.emplace_back(path(paths, 1, {}, pathType, width));

                            // Update current point
                            //cout << "currentPt: (" << xEnd << ", " << yEnd << ")" << endl;
                            currentPt = { xEnd, yEnd };
                            break;
                        }
                        case 02:
                            // Move operation

                            // Update current point
                            currentPt = { xEnd, yEnd };
                            break;
                        case 03:
                            // Flash operation (supposedly not allowed in region mode)

                            // Update current point
                            currentPt = { xEnd, yEnd };

                            // Push polygon boundary of aperture image to geometric cell
                            cellGerb.boundaries.emplace_back(currentAper.drawAsBound(xEnd, yEnd));
                            break;
                        }
                    }
                }
                // Enable region mode (starting with G-code "G36" in RS-274X)
                else if (fileLine.compare(0, 3, "G36") == 0)
                {
                    regionMode = true;
                }
                // Disable region mode (starting with G-code "G37" in RS-274X)
                else if (fileLine.compare(0, 3, "G37") == 0)
                {
                    regionMode = false;
                }
                // Enable single quadrant mode (starting with G-code "G74" in RS-274X)
                else if (fileLine.compare(0, 3, "G74") == 0)
                {
                    singleQuadMode = true;
                }
                // Enable multi quadrant mode (starting with G-code "G75" in RS-274X)
                else if (fileLine.compare(0, 3, "G75") == 0)
                {
                    singleQuadMode = false;
                }
                // Handle D-code operations with current aperture and settings starting with x-coordinate of end point
                else if (fileLine.compare(0, 1, "X") == 0)
                {
                    // Find delimiters
                    size_t indXEnd = fileLine.find("X"); // Ending x-coordinate delimiter
                    size_t indYEnd = fileLine.find("Y"); // Ending y-coordinate delimiter
                    size_t indIOff = fileLine.find("I"); // Arc offset distance in x-direction
                    size_t indJOff = fileLine.find("J"); // Arc offset distance in y-direction
                    size_t indDCOp = fileLine.find("D"); // D-code operation delimiter
                    size_t indGraphicClose = fileLine.find("*");

                    // Extract numbers
                    double xEnd = currentPt[0]; // Initialize ending point to current point
                    double yEnd = currentPt[1];
                    double iOff = 0.; // Initialize as zero offset from starting point to arc center (nonnegative for single quadrant, signed for multi quadrant)
                    double jOff = 0.;
                    if ((indXEnd != string::npos) && (indYEnd != string::npos))
                    {
                        xEnd = stod(fileLine.substr(indXEnd + 1, indYEnd - indXEnd - 1)) / pow(10.0, fracPartX) * adbGerb.getdbUnits(); // Length is index difference minus included characters with fractional part correction
                    }
                    else if ((indXEnd != string::npos) && (indYEnd == string::npos) && (indIOff != string::npos))
                    {
                        xEnd = stod(fileLine.substr(indXEnd + 1, indIOff - indXEnd - 1)) / pow(10.0, fracPartX) * adbGerb.getdbUnits();
                    }
                    else if ((indXEnd != string::npos) && (indYEnd == string::npos) && (indIOff == string::npos) && (indJOff != string::npos))
                    {
                        xEnd = stod(fileLine.substr(indXEnd + 1, indJOff - indXEnd - 1)) / pow(10.0, fracPartX) * adbGerb.getdbUnits();
                    }
                    else
                    {
                        xEnd = stod(fileLine.substr(indXEnd + 1, indDCOp - indXEnd - 1)) / pow(10.0, fracPartX) * adbGerb.getdbUnits();
                    }
                    if ((indYEnd != string::npos) && (indIOff != string::npos))
                    {
                        yEnd = stod(fileLine.substr(indYEnd + 1, indIOff - indYEnd - 1)) / pow(10.0, fracPartY) * adbGerb.getdbUnits();
                    }
                    else if ((indYEnd != string::npos) && (indIOff == string::npos) && (indJOff != string::npos))
                    {
                        yEnd = stod(fileLine.substr(indYEnd + 1, indJOff - indYEnd - 1)) / pow(10.0, fracPartY) * adbGerb.getdbUnits();
                    }
                    else
                    {
                        yEnd = stod(fileLine.substr(indYEnd + 1, indDCOp - indYEnd - 1)) / pow(10.0, fracPartY) * adbGerb.getdbUnits();
                    }
                    if ((indIOff != string::npos) && (indJOff != string::npos))
                    {
                        iOff = stod(fileLine.substr(indIOff + 1, indJOff - indIOff - 1)) / pow(10.0, fracPartX) * adbGerb.getdbUnits();
                    }
                    else
                    {
                        iOff = stod(fileLine.substr(indIOff + 1, indDCOp - indIOff - 1)) / pow(10.0, fracPartX) * adbGerb.getdbUnits();
                    }
                    if (indJOff != string::npos)
                    {
                        jOff = stod(fileLine.substr(indJOff + 1, indDCOp - indJOff - 1)) / pow(10.0, fracPartY) * adbGerb.getdbUnits();
                    }
                    int DCOp = stoi(fileLine.substr(indDCOp + 1, indGraphicClose - indDCOp - 1));

                    // Perform D-code operation
                    switch (DCOp)
                    {
                    case 01:
                    {
                        // Interpolate operation
                        int pathType; // pathType: 0 = square ends at vertices, 1 = round ends, 2 = square ends overshoot vertices by half width
                        if ((currentAper.getStanTemp() == 'C') || (currentAper.getStanTemp() == 'O'))
                        {
                            pathType = 1;
                        }
                        else
                        {
                            pathType = 2;
                        }
                        double width = currentAper.getCircumDia(); // Worst case scenario used

                        // Act based on active interpolation mode
                        if (graphicsMode == 1)
                        {
                            // Push new path to the geometric cell
                            cellGerb.paths.emplace_back(path({ currentPt[0], currentPt[1], xEnd, yEnd }, 1, {}, pathType, width));
                        }
                        else if ((graphicsMode == 2) || (graphicsMode == 3))
                        {
                            // Calculate center of circular arc, radius, and angle covered
                            double xStart = currentPt[0];
                            double yStart = currentPt[1];
                            double xCent = xStart; // Initialize to no offset from starting point
                            double yCent = yStart;
                            double arcRad = 0.0;
                            double startAngle = 0.0;
                            double endAngle = 0.0;
                            double arcAngle = 0.0;
                            if (singleQuadMode)
                            {
                                if (graphicsMode == 2)
                                {
                                    // Single quadrant mode has unsigned offsets to center in CW interpolation
                                    if ((xEnd <= xStart) && (yEnd >= yStart))
                                    {
                                        // Arc moves back and up
                                        xCent += iOff;
                                        yCent += jOff;
                                    }
                                    else if ((xEnd <= xStart) && (yEnd <= yStart))
                                    {
                                        // Arc moves back and down
                                        xCent -= iOff;
                                        yCent += jOff;
                                    }
                                    else if ((xEnd >= xStart) && (yEnd <= yStart))
                                    {
                                        // Arc moves forward and down
                                        xCent -= iOff;
                                        yCent -= jOff;
                                    }
                                    else if ((xEnd >= xStart) && (yEnd >= yStart))
                                    {
                                        // Arc moves forward and up
                                        xCent += iOff;
                                        yCent -= jOff;
                                    }
                                }
                                else
                                {
                                    // Single quadrant mode has unsigned offsets to center in CCW interpolation
                                    if ((xEnd <= xStart) && (yEnd >= yStart))
                                    {
                                        // Arc moves back and up
                                        xCent -= iOff;
                                        yCent -= jOff;
                                    }
                                    else if ((xEnd <= xStart) && (yEnd <= yStart))
                                    {
                                        // Arc moves back and down
                                        xCent += iOff;
                                        yCent -= jOff;
                                    }
                                    else if ((xEnd >= xStart) && (yEnd <= yStart))
                                    {
                                        // Arc moves forward and down
                                        xCent += iOff;
                                        yCent += jOff;
                                    }
                                    else if ((xEnd >= xStart) && (yEnd >= yStart))
                                    {
                                        // Arc moves forward and up
                                        xCent -= iOff;
                                        yCent += jOff;
                                    }
                                }
                                arcRad = 0.5 * (hypot(xStart - xCent, yStart - yCent) + hypot(xEnd - xCent, yEnd - yCent)); // Arithmetic mean because rounding errors prevent perfect circle
                                startAngle = atan2(yStart - yCent, xStart - xCent);
                                endAngle = atan2(yEnd - yCent, xEnd - xCent);
                                arcAngle = acos(((xStart - xCent) * (xEnd - xCent) + (yStart - yCent) * (yEnd - yCent)) / (hypot(xStart - xCent, yStart - yCent) * hypot(xEnd - xCent, yEnd - yCent))); // Dot product formula
                            }
                            else
                            {
                                // Multi quadrant mode has signed offsets to center
                                xCent += iOff;
                                yCent += jOff;
                                arcRad = 0.5 * (hypot(xStart - xCent, yStart - yCent) + hypot(xEnd - xCent, yEnd - yCent)); // Arithmetic mean because rounding errors prevent perfect circle
                                startAngle = atan2(yStart - yCent, xStart - xCent);
                                if (startAngle < 0)
                                {
                                    startAngle += 2.0 * M_PI; // Have to work entirely with angles between 0 and 2*pi
                                }
                                endAngle = atan2(yEnd - yCent, xEnd - xCent);
                                if (endAngle < 0)
                                {
                                    endAngle += 2.0 * M_PI; // Have to work entirely with angles between 0 and 2*pi
                                }
                                if (graphicsMode == 2)
                                {
                                    arcAngle = startAngle - endAngle; // Difference of angles CW
                                }
                                else
                                {
                                    arcAngle = endAngle - startAngle; // Difference of angles CCW
                                }
                            }

                            // Calculate points along arc with irregular 24-gon approximation
                            size_t nArcPt = ceil(arcAngle / (M_PI / 12.));
                            vector<double> paths; // Initialize vector of path coordinates
                            paths.push_back(xStart); // x-coordinate of current point
                            paths.push_back(yStart); // y-coordinate of current point
                            if (graphicsMode == 2)
                            {
                                for (size_t indi = 1; indi < nArcPt; indi++)
                                {
                                    paths.push_back(xCent + arcRad * cos(-2.0 * M_PI * indi / 24 + startAngle)); // x-coordinate CW
                                    paths.push_back(yCent + arcRad * sin(-2.0 * M_PI * indi / 24 + startAngle)); // y-coordinate CW
                                }
                            }
                            else
                            {
                                for (size_t indi = 1; indi < nArcPt; indi++)
                                {
                                    paths.push_back(xCent + arcRad * cos(+2.0 * M_PI * indi / 24 + startAngle)); // x-coordinate CCW
                                    paths.push_back(yCent + arcRad * sin(+2.0 * M_PI * indi / 24 + startAngle)); // y-coordinate CCW
                                }
                            }
                            paths.push_back(xEnd); // x-coordinate of end point
                            paths.push_back(yEnd); // y-coordinate of end point

                            // Push new path to the geometric cell
                            cellGerb.paths.emplace_back(path(paths, 1, {}, pathType, width));
                        }

                        // Update current point
                        currentPt = { xEnd, yEnd };
                        break;
                    }
                    case 02:
                        // Move operation

                        // Update current point
                        currentPt = { xEnd, yEnd };
                        break;
                    case 03:
                        // Flash operation (supposedly not allowed in region mode)

                        // Update current point
                        currentPt = { xEnd, yEnd };

                        // Push polygon boundary of aperture image to geometric cell
                        cellGerb.boundaries.emplace_back(currentAper.drawAsBound(xEnd, yEnd));
                        break;
                    }
                }
                // Handle D-code operations with current aperture and settings starting with y-coordinate of end point
                else if (fileLine.compare(0, 1, "Y") == 0)
                {
                    // Find delimiters
                    size_t indYEnd = fileLine.find("Y"); // Ending y-coordinate delimiter
                    size_t indIOff = fileLine.find("I"); // Arc offset distance in x-direction
                    size_t indJOff = fileLine.find("J"); // Arc offset distance in y-direction
                    size_t indDCOp = fileLine.find("D"); // D-code operation delimiter
                    size_t indGraphicClose = fileLine.find("*");

                    // Extract numbers
                    double xEnd = currentPt[0]; // Ending point must have same x-coordinate as starting point if not given
                    double yEnd = currentPt[1]; // Initialize ending point to current point
                    double iOff = 0.; // Initialize as zero offset from starting point to arc center (nonnegative for single quadrant, signed for multi quadrant)
                    double jOff = 0.;
                    if ((indYEnd != string::npos) && (indIOff != string::npos))
                    {
                        yEnd = stod(fileLine.substr(indYEnd + 1, indIOff - indYEnd - 1)) / pow(10.0, fracPartY) * adbGerb.getdbUnits();
                    }
                    else if ((indYEnd != string::npos) && (indIOff == string::npos) && (indJOff != string::npos))
                    {
                        yEnd = stod(fileLine.substr(indYEnd + 1, indJOff - indYEnd - 1)) / pow(10.0, fracPartY) * adbGerb.getdbUnits();
                    }
                    else
                    {
                        yEnd = stod(fileLine.substr(indYEnd + 1, indDCOp - indYEnd - 1)) / pow(10.0, fracPartY) * adbGerb.getdbUnits();
                    }
                    if ((indIOff != string::npos) && (indJOff != string::npos))
                    {
                        iOff = stod(fileLine.substr(indIOff + 1, indJOff - indIOff - 1)) / pow(10.0, fracPartX) * adbGerb.getdbUnits();
                    }
                    else
                    {
                        iOff = stod(fileLine.substr(indIOff + 1, indDCOp - indIOff - 1)) / pow(10.0, fracPartX) * adbGerb.getdbUnits();
                    }
                    if (indJOff != string::npos)
                    {
                        jOff = stod(fileLine.substr(indJOff + 1, indDCOp - indJOff - 1)) / pow(10.0, fracPartY) * adbGerb.getdbUnits();
                    }
                    int DCOp = stoi(fileLine.substr(indDCOp + 1, indGraphicClose - indDCOp - 1));

                    // Perform D-code operation
                    switch (DCOp)
                    {
                    case 01:
                    {
                        // Interpolate operation
                        int pathType; // pathType: 0 = square ends at vertices, 1 = round ends, 2 = square ends overshoot vertices by half width
                        if ((currentAper.getStanTemp() == 'C') || (currentAper.getStanTemp() == 'O'))
                        {
                            pathType = 1;
                        }
                        else
                        {
                            pathType = 2;
                        }
                        double width = currentAper.getCircumDia(); // Worst case scenario used

                                                                   // Act based on active interpolation mode
                        if (graphicsMode == 1)
                        {
                            // Push new path to the geometric cell
                            cellGerb.paths.emplace_back(path({ currentPt[0], currentPt[1], xEnd, yEnd }, 1, {}, pathType, width));
                        }
                        else if ((graphicsMode == 2) || (graphicsMode == 3))
                        {
                            // Calculate center of circular arc, radius, and angle covered
                            double xStart = currentPt[0];
                            double yStart = currentPt[1];
                            double xCent = xStart; // Initialize to no offset from starting point
                            double yCent = yStart;
                            double arcRad = 0.0;
                            double startAngle = 0.0;
                            double endAngle = 0.0;
                            double arcAngle = 0.0;
                            if (singleQuadMode)
                            {
                                if (graphicsMode == 2)
                                {
                                    // Single quadrant mode has unsigned offsets to center in CW interpolation
                                    if ((xEnd <= xStart) && (yEnd >= yStart))
                                    {
                                        // Arc moves back and up
                                        xCent += iOff;
                                        yCent += jOff;
                                    }
                                    else if ((xEnd <= xStart) && (yEnd <= yStart))
                                    {
                                        // Arc moves back and down
                                        xCent -= iOff;
                                        yCent += jOff;
                                    }
                                    else if ((xEnd >= xStart) && (yEnd <= yStart))
                                    {
                                        // Arc moves forward and down
                                        xCent -= iOff;
                                        yCent -= jOff;
                                    }
                                    else if ((xEnd >= xStart) && (yEnd >= yStart))
                                    {
                                        // Arc moves forward and up
                                        xCent += iOff;
                                        yCent -= jOff;
                                    }
                                }
                                else
                                {
                                    // Single quadrant mode has unsigned offsets to center in CCW interpolation
                                    if ((xEnd <= xStart) && (yEnd >= yStart))
                                    {
                                        // Arc moves back and up
                                        xCent -= iOff;
                                        yCent -= jOff;
                                    }
                                    else if ((xEnd <= xStart) && (yEnd <= yStart))
                                    {
                                        // Arc moves back and down
                                        xCent += iOff;
                                        yCent -= jOff;
                                    }
                                    else if ((xEnd >= xStart) && (yEnd <= yStart))
                                    {
                                        // Arc moves forward and down
                                        xCent += iOff;
                                        yCent += jOff;
                                    }
                                    else if ((xEnd >= xStart) && (yEnd >= yStart))
                                    {
                                        // Arc moves forward and up
                                        xCent -= iOff;
                                        yCent += jOff;
                                    }
                                }
                                arcRad = 0.5 * (hypot(xStart - xCent, yStart - yCent) + hypot(xEnd - xCent, yEnd - yCent)); // Arithmetic mean because rounding errors prevent perfect circle
                                startAngle = atan2(yStart - yCent, xStart - xCent);
                                endAngle = atan2(yEnd - yCent, xEnd - xCent);
                                arcAngle = acos(((xStart - xCent) * (xEnd - xCent) + (yStart - yCent) * (yEnd - yCent)) / (hypot(xStart - xCent, yStart - yCent) * hypot(xEnd - xCent, yEnd - yCent))); // Dot product formula
                            }
                            else
                            {
                                // Multi quadrant mode has signed offsets to center
                                xCent += iOff;
                                yCent += jOff;
                                arcRad = 0.5 * (hypot(xStart - xCent, yStart - yCent) + hypot(xEnd - xCent, yEnd - yCent)); // Arithmetic mean because rounding errors prevent perfect circle
                                startAngle = atan2(yStart - yCent, xStart - xCent);
                                if (startAngle < 0)
                                {
                                    startAngle += 2.0 * M_PI; // Have to work entirely with angles between 0 and 2*pi
                                }
                                endAngle = atan2(yEnd - yCent, xEnd - xCent);
                                if (endAngle < 0)
                                {
                                    endAngle += 2.0 * M_PI; // Have to work entirely with angles between 0 and 2*pi
                                }
                                if (graphicsMode == 2)
                                {
                                    arcAngle = startAngle - endAngle; // Difference of angles CW
                                }
                                else
                                {
                                    arcAngle = endAngle - startAngle; // Difference of angles CCW
                                }
                            }

                            // Calculate points along arc with irregular 24-gon approximation
                            size_t nArcPt = ceil(arcAngle / (M_PI / 12.));
                            vector<double> paths; // Initialize vector of path coordinates
                            paths.push_back(xStart); // x-coordinate of current point
                            paths.push_back(yStart); // y-coordinate of current point
                            if (graphicsMode == 2)
                            {
                                for (size_t indi = 1; indi < nArcPt; indi++)
                                {
                                    paths.push_back(xCent + arcRad * cos(-2.0 * M_PI * indi / 24 + startAngle)); // x-coordinate CW
                                    paths.push_back(yCent + arcRad * sin(-2.0 * M_PI * indi / 24 + startAngle)); // y-coordinate CW
                                }
                            }
                            else
                            {
                                for (size_t indi = 1; indi < nArcPt; indi++)
                                {
                                    paths.push_back(xCent + arcRad * cos(+2.0 * M_PI * indi / 24 + startAngle)); // x-coordinate CCW
                                    paths.push_back(yCent + arcRad * sin(+2.0 * M_PI * indi / 24 + startAngle)); // y-coordinate CCW
                                }
                            }
                            paths.push_back(xEnd); // x-coordinate of end point
                            paths.push_back(yEnd); // y-coordinate of end point

                            // Push new path to the geometric cell
                            cellGerb.paths.emplace_back(path(paths, 1, {}, pathType, width));
                        }

                        // Update current point
                        currentPt = { xEnd, yEnd };
                        break;
                    }
                    case 02:
                        // Move operation

                        // Update current point
                        currentPt = { xEnd, yEnd };
                        break;
                    case 03:
                        // Flash operation (supposedly not allowed in region mode)

                        // Update current point
                        currentPt = { xEnd, yEnd };

                        // Push polygon boundary of aperture image to geometric cell
                        cellGerb.boundaries.emplace_back(currentAper.drawAsBound(xEnd, yEnd));
                        break;
                    }
                }
                // End of file (starting with command "M02" in RS-274X)
                else if (fileLine.compare(0, 3, "M02") == 0)
                {
                    break;
                }

                // Keep reading new lines in file, skipping comments
                getline(outlineFile, fileLine);
                while (fileLine.compare(0, 3, "G04") == 0)
                {
                    getline(outlineFile, fileLine);
                }
            }

            // Close file
            outlineFile.close();

            // Update ASCII database of Gerber outline
            adbGerb.setLibName("outline");
            adbGerb.appendCell(cellGerb);
            adbGerb.setdbUnits(adbGerb.getdbUnits() * 1.e-3); // Rescale Gerber file units 0.001x to allow integer representation in GDSII
            vector<complex<double>> hullPt = adbGerb.convexHull(cellGerb.cellName);

            // Print and dump the ASCII database of Gerber outline
            //adbGerb.print({ });
            adbGerb.dump();

            // Extract coordinates from successful Graham scan convex hull process after Gerber outline file read
            cout << "Gerber outline file produced a convex hull of " << hullPt.size() << " points for the design limits" << endl;
            vector<double> hullCoord;
            for (size_t indi = 0; indi < hullPt.size(); indi++)
            {
                hullCoord.push_back(hullPt[indi].real()); // Real part is x-coordinate of a point
                hullCoord.push_back(hullPt[indi].imag()); // Imaginary part is y-coordinate of a point
            }

            // Optionally create new ASCII database of convex hull
            GeoCell cellHull;
            cellHull.boundaries.emplace_back(boundary(hullCoord, 1, { })); // Single boundary of convex hull
            cellHull.boundaries.back().reorder();
            AsciiDataBase adbHull;
            adbHull.setFileName(outlineGerberName.substr(0, outlineGerberName.find_last_of(".")) + "_HULL.gds");
            adbHull.setLibName("convexHull");
            adbHull.appendCell(cellHull);
            adbHull.setdbUnits(adbGerb.getdbUnits());
            adbHull.dump();

            // Return the convex hull coordinates
            return hullCoord;
        }
        else
        {
            // File could not be opened
            cerr << "Unable to open Gerber outline file" << endl;
            return { };
        }
    }

    // Read Excellon Numeric Control drill file (extension must be included)
    AsciiDataBase readHoles(std::string drillName)
    {
        // Attempt to open Excellon Numeric Control drill file
        ifstream drillFile(drillName.c_str());
        if (drillFile.is_open())
        {
            // Build single geometric cell from limboint.h to store information (forget about timestamps)
            GeoCell cellDrill;
            cellDrill.cellName = "drill";

            // Build ASCII database from limboint.h (forget about timestamps)
            AsciiDataBase adbDrill;
            adbDrill.setFileName(drillName.substr(0, drillName.find_last_of(".")) + ".gds");

            // Excellon NC drill file is barely readable line-by-line
            std::string fileLine;
            getline(drillFile, fileLine);

            // Skip all comment lines (starting with ";" in IPC-NC-349)
            while (fileLine.compare(0, 1, ";") == 0)
            {
                getline(drillFile, fileLine);
            }

            // Initialize file-scope variables
            bool leadingZeros = true; // true = leading zeros in coordinate date, false = trailing zeros in coordinate data
            bool incrementalInput = false; // true = incremental (relative) input of part program coordinates, false = absolute input of part program coordinates (unimplemented)
            int axisVersion = 2; // Should be 1 or 2
            int commandsFormat = 2; // Should be 1 or 2
            int intPart = 2; // Constrained to be 2 (iff "INCH"), 3 ("METRIC"), or 4 ("METRIC" in unimplemented CNC-7 machine)
            int fracPart = 4; // Constrained to be 2 ("METRIC"), 3 ("METRIC"), or 4 (iff "INCH")
            bool drillMode = true; // true = drill mode, false = rout mode
            bool routToolDown = false; // Tool down for routing in true and in rout mode
            int graphicsMode = 0; // Active routing modes are "G00" (rout mode rapid positioning), "G01" (linear interpolation), "G02" (CW circular interp.), and "G03" (CCW circular interp.)
            double defaultRadius = 0.; // Default radius for circular interpolations in rout mode
            vector<double> currentPt = { 0., 0. }; // Coordinates of current point
            vector<Aperture> customTool = {}; // Custom tool bits
            Aperture currentTool = Aperture(); // Current tool bit in use

            // Read rest of file line-by-line with very rough parser
            while (!drillFile.eof())
            {
                // Handle header beginning (starting with command "M48" in IPC-NC-349)
                if (fileLine.compare(0, 3, "M48") == 0)
                {
                    //cout << "Begin reading Excellon NC drill file header" << endl;
                }
                // Handle SI metric units (starting with command "METRIC" in IPC-NC-349)
                else if (fileLine.compare(0, 6, "METRIC") == 0)
                {
                    double multSI = 1.e-3; // Units are in millimeters
                    intPart = 3; // 3 places before the implied decimal (assumed for this CNC-& machine over possible 4 places in other precision)
                    fracPart = 3; // Precision to 0.001 mm (assumed for this CNC-7 machine over precision of 0.01 mm)

                    if (fileLine.length() >= 9)
                    {
                        // Coordinate data has leading zeros
                        if (fileLine.compare(7, 2, "LZ") == 0)
                        {
                            leadingZeros = true;
                        }
                        // Coordinate data has trailing zeros
                        else if (fileLine.compare(7, 2, "TZ") == 0)
                        {
                            leadingZeros = false;
                        }
                    }

                    // Propagate units information to ASCII database now
                    adbDrill.setdbUserUnits(1000.);
                    adbDrill.setdbUnits(multSI);
                }
                // Handle US customary units (starting with command "INCH" in IPC-NC-349)
                else if (fileLine.compare(0, 4, "INCH") == 0)
                {
                    double multSI = 0.0254; // Units are in inches
                    intPart = 2; // 2 places before the implied decimal
                    fracPart = 4; // Precision to 0.0001 in.

                    if (fileLine.length() >= 7)
                    {
                        // Coordinate data has leading zeros
                        if (fileLine.compare(5, 2, "LZ") == 0)
                        {
                            leadingZeros = true;
                        }
                        // Coordinate data has trailing zeros
                        else if (fileLine.compare(5, 2, "TZ") == 0)
                        {
                            leadingZeros = false;
                        }
                    }

                    // Propagate units information to ASCII database now
                    adbDrill.setdbUserUnits(10000.);
                    adbDrill.setdbUnits(multSI);
                }
                // Handle part program coordinates input (starting with command "ICI" in IPC-NC-349)
                else if (fileLine.compare(0, 3, "ICI") == 0)
                {
                    incrementalInput = true;
                }
                // Handle axis layout version (starting with command "VER" in IPC-NC-349)
                else if (fileLine.compare(0, 4, "VER,") == 0)
                {
                    axisVersion = stoi(fileLine.substr(4, string::npos));
                }
                // Handle commands format (starting with command "FMAT" in IPC-NC-349)
                else if (fileLine.compare(0, 5, "FMAT,") == 0)
                {
                    commandsFormat = stoi(fileLine.substr(5, string::npos));
                }
                // Define a tool or select a tool (declared with command "T" in IPC-NC-349)
                else if (fileLine.compare(0, 1, "T") == 0)
                {
                    // See if any additional information on this line (header or body determination)
                    fileLine = fileLine.substr(0, fileLine.find(";")); // Trim off any comment
                    if (fileLine.length() > 2)
                    {
                        // Find delimiters
                        size_t indFeed = fileLine.find("F"); // Z-axis worktable infeed rate (in./min or mm/ss, Excellon only)
                        size_t indBRet = fileLine.find("B"); // Spindle retract rate (in./min or mm/ss, Excellon only)
                        size_t indSSpd = fileLine.find("S"); // Spindle rotational speed (thousands of RPM, Excellon only)
                        size_t indCDia = fileLine.find("C"); // Hole diameter after any plating (thousandths of in. or micrometers)
                        size_t indHitN = fileLine.find("H"); // Maximum hit count before tool bit replaced (Excellon only)
                        size_t indZOff = fileLine.find("Z"); // Depth offset for tools compared to mean depth (thousandths of in. or 0.01 mm, Excellon only)

                        // Extract numbers (default feeds and speeds would be loaded from tool diameter table on CNC-7 machine)
                        size_t indToolNumEnd = min(indFeed, min(indBRet, min(indSSpd, min(indCDia, min(indHitN, indZOff))))) - 1; // Tool number ends right before the first delimiter
                        int toolNum = stoi(fileLine.substr(1, indToolNumEnd)); // Tool number
                        size_t indToolDiaEnd = min(((indFeed > indCDia) ? indFeed : string::npos), min(((indBRet > indCDia) ? indBRet : string::npos), min(((indSSpd > indCDia) ? indSSpd : string::npos), min(((indHitN > indCDia) ? indHitN : string::npos), ((indZOff > indCDia) ? indZOff : string::npos))))) - 1; // Tool diameter ends right before the first delimeter found after its own delimeter (ternary operator ensures minimum index truly is after indCDia)
                        double toolDia = stod(fileLine.substr(indCDia + 1, indToolDiaEnd - indCDia)) * adbDrill.getdbUnits(); // Hole diameter is index difference minus zero included characters with fractional part correction

                        // Repurpose standard aperture template for a circular aperture without a hole
                        customTool.emplace_back(Aperture(toolNum, toolDia, 0.0));
                    }
                    else
                    {
                        // Find number of current tool to select
                        int toolNum = stoi(fileLine.substr(1, string::npos));

                        // Look up the tool among already defined tools in Aperture class
                        for (size_t indTool = 0; indTool < customTool.size(); indTool++)
                        {
                            if (toolNum == customTool[indTool].getAperNum())
                            {
                                // Switch to tool
                                currentTool = customTool[indTool];
                            }
                        }
                    }
                }
                // Handle header override (starting with command "OM48" in IPC-NC-349)
                else if (fileLine.compare(0, 4, "OM48") == 0)
                {
                    //cout << "Override the Excellon NC drill file header" << endl;
                }
                // Handle header ending (starting with rewind stop command "%" in IPC-NC-349 or command "M95" in Excellon)
                else if ((fileLine.compare(0, 1, "%") == 0) || (fileLine.compare(0, 3, "M95") == 0))
                {
                    //cout << "Done reading Excellon NC drill file header" << endl;
                }
                // Switch graphics mode to rout mode and position tool bit (starting with G-code "G00" in Excellon)
                else if (fileLine.compare(0, 3, "G00") == 0)
                {
                    drillMode = false;
                    graphicsMode = 0;

                    // See if any additional information on this line
                    size_t indComment = fileLine.find(";");
                    if (indComment > 3)
                    {
                        // Find delimiters
                        size_t indXEnd = fileLine.find("X"); // Ending x-coordinate delimiter
                        size_t indYEnd = fileLine.find("Y"); // Ending y-coordinate delimiter

                        // Extract numbers
                        double xEnd = currentPt[0]; // Initialize ending point to current point
                        double yEnd = currentPt[1];
                        if ((indXEnd != string::npos) && (indYEnd != string::npos))
                        {
                            xEnd = stod(fileLine.substr(indXEnd + 1, indYEnd - indXEnd - 1)) * adbDrill.getdbUnits();
                            if (leadingZeros)
                            {
                                xEnd *= pow(10.0, (indYEnd - indXEnd) - intPart); // The number of digits given minus the expected integer part is the power of ten in the correction factor in leading zeros mode
                            }
                            else
                            {
                                xEnd *= pow(10.0, -fracPart); // Correction factor in trailing zeros mode is just the precision of the given number
                            }
                        }
                        else if ((indXEnd != string::npos) && (indYEnd == string::npos))
                        {
                            xEnd = stod(fileLine.substr(indXEnd + 1, indComment)) * adbDrill.getdbUnits();
                            if (leadingZeros)
                            {
                                xEnd *= pow(10.0, fileLine.substr(indXEnd + 1, indComment).length() - intPart);
                            }
                            else
                            {
                                xEnd *= pow(10.0, -fracPart);
                            }
                        }
                        if (indYEnd != string::npos)
                        {
                            yEnd = stod(fileLine.substr(indYEnd + 1, indComment)) * adbDrill.getdbUnits();
                            if (leadingZeros)
                            {
                                yEnd *= pow(10.0, fileLine.substr(indYEnd + 1, indComment).length() - intPart);
                            }
                            else
                            {
                                yEnd *= pow(10.0, -fracPart);
                            }
                        }

                        // Perform rapid positioning
                        currentPt = { xEnd, yEnd }; // Update current point
                    }
                }
                // Switch graphics mode to linear interpolation (starting with G-code "G01" in IPC-NC-349)
                else if (fileLine.compare(0, 3, "G01") == 0)
                {
                    graphicsMode = 1;

                    // See if any additional information on this line
                    size_t indComment = fileLine.find(";");
                    if (indComment > 3)
                    {
                        // Find delimiters
                        size_t indXEnd = fileLine.find("X"); // Ending x-coordinate delimiter
                        size_t indYEnd = fileLine.find("Y"); // Ending y-coordinate delimiter

                        // Extract numbers
                        double xEnd = currentPt[0]; // Initialize ending point to current point
                        double yEnd = currentPt[1];
                        if ((indXEnd != string::npos) && (indYEnd != string::npos))
                        {
                            xEnd = stod(fileLine.substr(indXEnd + 1, indYEnd - indXEnd - 1)) * adbDrill.getdbUnits();
                            if (leadingZeros)
                            {
                                xEnd *= pow(10.0, (indYEnd - indXEnd) - intPart); // The number of digits given minus the expected integer part is the power of ten in the correction factor in leading zeros mode
                            }
                            else
                            {
                                xEnd *= pow(10.0, -fracPart); // Correction factor in trailing zeros mode is just the precision of the given number
                            }
                        }
                        else if ((indXEnd != string::npos) && (indYEnd == string::npos))
                        {
                            xEnd = stod(fileLine.substr(indXEnd + 1, indComment)) * adbDrill.getdbUnits();
                            if (leadingZeros)
                            {
                                xEnd *= pow(10.0, fileLine.substr(indXEnd + 1, indComment).length() - intPart);
                            }
                            else
                            {
                                xEnd *= pow(10.0, -fracPart);
                            }
                        }
                        if (indYEnd != string::npos)
                        {
                            yEnd = stod(fileLine.substr(indYEnd + 1, indComment)) * adbDrill.getdbUnits();
                            if (leadingZeros)
                            {
                                yEnd *= pow(10.0, fileLine.substr(indYEnd + 1, indComment).length() - intPart);
                            }
                            else
                            {
                                yEnd *= pow(10.0, -fracPart);
                            }
                        }

                        // Perform full rout interpolation operation
                        if (!drillMode && routToolDown)
                        {
                            int pathType = 1; // Round ends on GDSII path element
                            double width = currentTool.getCircumDia(); // Width of path will be diameter of tool bit
                            cellDrill.paths.emplace_back(path({ currentPt[0], currentPt[1], xEnd, yEnd }, 1, {}, pathType, width)); // Push new path to geometric cell
                            currentPt = { xEnd, yEnd }; // Update current point
                        }
                    }
                }
                // Switch graphics mode to clockwise circular interpolation (starting with G-code "G02" in IPC-NC-349)
                else if (fileLine.compare(0, 3, "G02") == 0)
                {
                    graphicsMode = 2;

                    // See if any additional information on this line
                    size_t indComment = fileLine.find(";");
                    if (indComment > 3)
                    {
                        // Find delimiters
                        size_t indXEnd = fileLine.find("X"); // Ending x-coordinate delimiter
                        size_t indYEnd = fileLine.find("Y"); // Ending y-coordinate delimiter
                        size_t indIOff = fileLine.find("I"); // Arc offset distance in x-direction (Excellon only)
                        size_t indJOff = fileLine.find("J"); // Arc offset distance in y-direction (Excellon only)
                        size_t indARad = fileLine.find("A"); // Arc radius

                        // Extract numbers
                        double xEnd = currentPt[0]; // Initialize ending point to current point
                        double yEnd = currentPt[1];
                        double iOff = 0.; // Initialize as zero offset from starting point to arc center
                        double jOff = 0.;
                        double aRad = 0.; // Initialize as zero arc radius
                        if ((indXEnd != string::npos) && (indYEnd != string::npos))
                        {
                            xEnd = stod(fileLine.substr(indXEnd + 1, indYEnd - indXEnd - 1)) * adbDrill.getdbUnits();
                            if (leadingZeros)
                            {
                                xEnd *= pow(10.0, (indYEnd - indXEnd) - intPart); // The number of digits given minus the expected integer part is the power of ten in the correction factor in leading zeros mode
                            }
                            else
                            {
                                xEnd *= pow(10.0, -fracPart); // Correction factor in trailing zeros mode is just the precision of the given number
                            }
                        }
                        else if ((indXEnd != string::npos) && (indYEnd == string::npos) && (indIOff != string::npos))
                        {
                            xEnd = stod(fileLine.substr(indXEnd + 1, indIOff - indXEnd - 1)) * adbDrill.getdbUnits();
                            if (leadingZeros)
                            {
                                xEnd *= pow(10.0, (indIOff - indXEnd) - intPart);
                            }
                            else
                            {
                                xEnd *= pow(10.0, -fracPart);
                            }
                        }
                        else
                        {
                            xEnd = stod(fileLine.substr(indXEnd + 1, indARad - indXEnd - 1)) * adbDrill.getdbUnits();
                            if (leadingZeros)
                            {
                                xEnd *= pow(10.0, (indARad - indXEnd) - intPart);
                            }
                            else
                            {
                                xEnd *= pow(10.0, -fracPart);
                            }
                        }
                        if ((indYEnd != string::npos) && (indIOff != string::npos))
                        {
                            yEnd = stod(fileLine.substr(indYEnd + 1, indIOff - indYEnd - 1)) * adbDrill.getdbUnits();
                            if (leadingZeros)
                            {
                                yEnd *= pow(10.0, (indIOff - indYEnd) - intPart);
                            }
                            else
                            {
                                yEnd *= pow(10.0, -fracPart);
                            }
                        }
                        else
                        {
                            yEnd = stod(fileLine.substr(indYEnd + 1, indARad - indYEnd - 1)) * adbDrill.getdbUnits();
                            if (leadingZeros)
                            {
                                yEnd *= pow(10.0, (indARad - indYEnd) - intPart);
                            }
                            else
                            {
                                yEnd *= pow(10.0, -fracPart);
                            }
                        }
                        if (indIOff != string::npos)
                        {
                            iOff = stod(fileLine.substr(indIOff + 1, indJOff - indIOff - 1)) * adbDrill.getdbUnits();
                            if (leadingZeros)
                            {
                                iOff *= pow(10.0, (indJOff - indIOff) - intPart);
                            }
                            else
                            {
                                iOff *= pow(10.0, -fracPart);
                            }
                        }
                        if (indJOff != string::npos)
                        {
                            jOff = stod(fileLine.substr(indJOff + 1, indComment)) * adbDrill.getdbUnits();
                            if (leadingZeros)
                            {
                                jOff *= pow(10.0, fileLine.substr(indJOff + 1, indComment).length() - intPart);
                            }
                            else
                            {
                                jOff *= pow(10.0, -fracPart);
                            }
                        }
                        if (indARad != string::npos)
                        {
                            aRad = stod(fileLine.substr(indARad + 1, indComment)) * adbDrill.getdbUnits();
                            if (leadingZeros)
                            {
                                aRad *= pow(10.0, fileLine.substr(indARad + 1, indComment).length() - intPart);
                            }
                            else
                            {
                                aRad *= pow(10.0, -fracPart);
                            }
                            defaultRadius = aRad; // Update the default radius
                        }

                        // Perform full rout interpolation operation
                        if (!drillMode && routToolDown)
                        {
                            // Interpolate operation (movement rate of 100 in./min at 100% feed rate by default)
                            int pathType = 1; // Round ends on GDSII path element
                            double width = currentTool.getCircumDia(); // Width of path will be diameter of tool bit

                            // Calculate center of circular arc, radius, and angle covered
                            double xStart = currentPt[0];
                            double yStart = currentPt[1];
                            double xCent = xStart; // Initialize to no offset from starting point
                            double yCent = yStart;
                            double arcRad = 0.0; // Initialize to zero arc radius to interpolate
                            if ((aRad != 0.) && (indIOff == string::npos) && (indJOff == string::npos))
                            {
                                arcRad = aRad; // Use the radius read from this line in the file
                                double aSemi = 0.5 * hypot(xEnd - xStart, yEnd - yStart); // Semidiagonal a (connecting arc points) of the rhombus connecting the start point, end point, and two possible circle centers
                                double bSemi = 0.0; // Semidiagonal b (connecting possible circle centers) of the same rhombus
                                if (arcRad > aSemi)
                                {
                                    bSemi = sqrt(pow(arcRad, 2.) - pow(aSemi, 2.)); // Valid radius
                                }
                                else
                                {
                                    arcRad = 2.0 * aSemi; // CNC-7 minimal adjusted radius
                                }
                                xCent += 0.5 * (xEnd - xStart) + bSemi / aSemi * 0.5 * (yEnd - yStart); // Center is start arc point + midpoint of arc points + scaled walk rotated -pi/2 rad
                                yCent += 0.5 * (xEnd - xStart) - bSemi / aSemi * 0.5 * (xEnd - xStart);
                            }
                            else if ((aRad == 0.) && (indIOff != string::npos) && (indJOff != string::npos))
                            {
                                xCent -= iOff; // Use offset coord = arc start coord - arc center coord
                                yCent -= jOff;
                                arcRad = 0.5 * (hypot(xStart - xCent, yStart - yCent) + hypot(xEnd - xCent, yEnd - yCent)); // Arithmetic mean because rounding errors prevent perfect circle
                            }
                            else
                            {
                                arcRad = defaultRadius; // Use the radius from a previous operation
                                double aSemi = 0.5 * hypot(xEnd - xStart, yEnd - yStart); // Semidiagonal a (connecting arc points) of the rhombus connecting the start point, end point, and two possible circle centers
                                double bSemi = 0.0; // Semidiagonal b (connecting possible circle centers) of the same rhombus
                                if (arcRad > aSemi)
                                {
                                    bSemi = sqrt(pow(arcRad, 2.) - pow(aSemi, 2.)); // Valid radius
                                }
                                else
                                {
                                    arcRad = 2.0 * aSemi; // CNC-7 minimal adjusted radius
                                }
                                xCent += 0.5 * (xEnd - xStart) + bSemi / aSemi * 0.5 * (yEnd - yStart); // Center is start arc point + midpoint of arc points + scaled walk rotated -pi/2 rad
                                yCent += 0.5 * (xEnd - xStart) - bSemi / aSemi * 0.5 * (xEnd - xStart);
                            }
                            double startAngle = atan2(yStart - yCent, xStart - xCent);
                            if (startAngle < 0)
                            {
                                startAngle += 2.0 * M_PI; // Have to work entirely with angles between 0 and 2*pi
                            }
                            double endAngle = atan2(yEnd - yCent, xEnd - xCent);
                            if (endAngle < 0)
                            {
                                endAngle += 2.0 * M_PI; // Have to work entirely with angles between 0 and 2*pi
                            }
                            double arcAngle = startAngle - endAngle; // Difference of angles CW

                            // Calculate points along arc with irregular 24-gon approximation
                            size_t nArcPt = ceil(arcAngle / (M_PI / 12.));
                            vector<double> paths; // Initialize vector of path coordinates
                            paths.push_back(xStart); // x-coordinate of current point
                            paths.push_back(yStart); // y-coordinate of current point
                            for (size_t indi = 1; indi < nArcPt; indi++)
                            {
                                paths.push_back(xCent + arcRad * cos(-2.0 * M_PI * indi / 24 + startAngle)); // x-coordinate CW
                                paths.push_back(yCent + arcRad * sin(-2.0 * M_PI * indi / 24 + startAngle)); // y-coordinate CW
                            }
                            paths.push_back(xEnd); // x-coordinate of end point
                            paths.push_back(yEnd); // y-coordinate of end point

                            // Finish interpolation by updating records and state
                            cellDrill.paths.emplace_back(path(paths, 1, {}, pathType, width)); // Push new path to geometric cell
                            currentPt = { xEnd, yEnd }; // Update current point
                        }
                    }
                }
                // Switch graphics mode to counterclockwise circular interpolation (starting with G-code "G03" in IPC-NC-349)
                else if (fileLine.compare(0, 3, "G03") == 0)
                {
                    graphicsMode = 3;

                    // See if any additional information on this line
                    size_t indComment = fileLine.find(";");
                    if (indComment > 3)
                    {
                        // Find delimiters
                        size_t indXEnd = fileLine.find("X"); // Ending x-coordinate delimiter
                        size_t indYEnd = fileLine.find("Y"); // Ending y-coordinate delimiter
                        size_t indIOff = fileLine.find("I"); // Arc offset distance in x-direction (Excellon only)
                        size_t indJOff = fileLine.find("J"); // Arc offset distance in y-direction (Excellon only)
                        size_t indARad = fileLine.find("A"); // Arc radius

                        // Extract numbers
                        double xEnd = currentPt[0]; // Initialize ending point to current point
                        double yEnd = currentPt[1];
                        double iOff = 0.; // Initialize as zero offset from starting point to arc center
                        double jOff = 0.;
                        double aRad = 0.; // Initialize as zero arc radius
                        if ((indXEnd != string::npos) && (indYEnd != string::npos))
                        {
                            xEnd = stod(fileLine.substr(indXEnd + 1, indYEnd - indXEnd - 1)) * adbDrill.getdbUnits();
                            if (leadingZeros)
                            {
                                xEnd *= pow(10.0, (indYEnd - indXEnd) - intPart); // The number of digits given minus the expected integer part is the power of ten in the correction factor in leading zeros mode
                            }
                            else
                            {
                                xEnd *= pow(10.0, -fracPart); // Correction factor in trailing zeros mode is just the precision of the given number
                            }
                        }
                        else if ((indXEnd != string::npos) && (indYEnd == string::npos) && (indIOff != string::npos))
                        {
                            xEnd = stod(fileLine.substr(indXEnd + 1, indIOff - indXEnd - 1)) * adbDrill.getdbUnits();
                            if (leadingZeros)
                            {
                                xEnd *= pow(10.0, (indIOff - indXEnd) - intPart);
                            }
                            else
                            {
                                xEnd *= pow(10.0, -fracPart);
                            }
                        }
                        else
                        {
                            xEnd = stod(fileLine.substr(indXEnd + 1, indARad - indXEnd - 1)) * adbDrill.getdbUnits();
                            if (leadingZeros)
                            {
                                xEnd *= pow(10.0, (indARad - indXEnd) - intPart);
                            }
                            else
                            {
                                xEnd *= pow(10.0, -fracPart);
                            }
                        }
                        if ((indYEnd != string::npos) && (indIOff != string::npos))
                        {
                            yEnd = stod(fileLine.substr(indYEnd + 1, indIOff - indYEnd - 1)) * adbDrill.getdbUnits();
                            if (leadingZeros)
                            {
                                yEnd *= pow(10.0, (indIOff - indYEnd) - intPart);
                            }
                            else
                            {
                                yEnd *= pow(10.0, -fracPart);
                            }
                        }
                        else
                        {
                            yEnd = stod(fileLine.substr(indYEnd + 1, indARad - indYEnd - 1)) * adbDrill.getdbUnits();
                            if (leadingZeros)
                            {
                                yEnd *= pow(10.0, (indARad - indYEnd) - intPart);
                            }
                            else
                            {
                                yEnd *= pow(10.0, -fracPart);
                            }
                        }
                        if (indIOff != string::npos)
                        {
                            iOff = stod(fileLine.substr(indIOff + 1, indJOff - indIOff - 1)) * adbDrill.getdbUnits();
                            if (leadingZeros)
                            {
                                iOff *= pow(10.0, (indJOff - indIOff) - intPart);
                            }
                            else
                            {
                                iOff *= pow(10.0, -fracPart);
                            }
                        }
                        if (indJOff != string::npos)
                        {
                            jOff = stod(fileLine.substr(indJOff + 1, indComment)) * adbDrill.getdbUnits();
                            if (leadingZeros)
                            {
                                jOff *= pow(10.0, fileLine.substr(indJOff + 1, indComment).length() - intPart);
                            }
                            else
                            {
                                jOff *= pow(10.0, -fracPart);
                            }
                        }
                        if (indARad != string::npos)
                        {
                            aRad = stod(fileLine.substr(indARad + 1, indComment)) * adbDrill.getdbUnits();
                            if (leadingZeros)
                            {
                                aRad *= pow(10.0, fileLine.substr(indARad + 1, indComment).length() - intPart);
                            }
                            else
                            {
                                aRad *= pow(10.0, -fracPart);
                            }
                            defaultRadius = aRad; // Update the default radius
                        }

                        // Perform full rout interpolation operation
                        if (!drillMode && routToolDown)
                        {
                            // Interpolate operation (movement rate of 100 in./min at 100% feed rate by default)
                            int pathType = 1; // Round ends on GDSII path element
                            double width = currentTool.getCircumDia(); // Width of path will be diameter of tool bit

                            // Calculate center of circular arc, radius, and angle covered
                            double xStart = currentPt[0];
                            double yStart = currentPt[1];
                            double xCent = xStart; // Initialize to no offset from starting point
                            double yCent = yStart;
                            double arcRad = 0.0; // Initialize to zero arc radius to interpolate
                            if ((aRad != 0.) && (indIOff == string::npos) && (indJOff == string::npos))
                            {
                                arcRad = aRad; // Use the radius read from this line in the file
                                double aSemi = 0.5 * hypot(xEnd - xStart, yEnd - yStart); // Semidiagonal a (connecting arc points) of the rhombus connecting the start point, end point, and two possible circle centers
                                double bSemi = 0.0; // Semidiagonal b (connecting possible circle centers) of the same rhombus
                                if (arcRad > aSemi)
                                {
                                    bSemi = sqrt(pow(arcRad, 2.) - pow(aSemi, 2.)); // Valid radius
                                }
                                else
                                {
                                    arcRad = 2.0 * aSemi; // CNC-7 minimal adjusted radius
                                }
                                xCent += 0.5 * (xEnd - xStart) - bSemi / aSemi * 0.5 * (yEnd - yStart); // Center is start arc point + midpoint of arc points + scaled walk rotated +pi/2 rad
                                yCent += 0.5 * (xEnd - xStart) + bSemi / aSemi * 0.5 * (xEnd - xStart);
                            }
                            else if ((aRad == 0.) && (indIOff != string::npos) && (indJOff != string::npos))
                            {
                                xCent -= iOff; // Use offset coord = arc start coord - arc center coord
                                yCent -= jOff;
                                arcRad = 0.5 * (hypot(xStart - xCent, yStart - yCent) + hypot(xEnd - xCent, yEnd - yCent)); // Arithmetic mean because rounding errors prevent perfect circle
                            }
                            else
                            {
                                arcRad = defaultRadius; // Use the radius from a previous operation
                                double aSemi = 0.5 * hypot(xEnd - xStart, yEnd - yStart); // Semidiagonal a (connecting arc points) of the rhombus connecting the start point, end point, and two possible circle centers
                                double bSemi = 0.0; // Semidiagonal b (connecting possible circle centers) of the same rhombus
                                if (arcRad > aSemi)
                                {
                                    bSemi = sqrt(pow(arcRad, 2.) - pow(aSemi, 2.)); // Valid radius
                                }
                                else
                                {
                                    arcRad = 2.0 * aSemi; // CNC-7 minimal adjusted radius
                                }
                                xCent += 0.5 * (xEnd - xStart) - bSemi / aSemi * 0.5 * (yEnd - yStart); // Center is start arc point + midpoint of arc points + scaled walk rotated +pi/2 rad
                                yCent += 0.5 * (xEnd - xStart) + bSemi / aSemi * 0.5 * (xEnd - xStart);
                            }
                            double startAngle = atan2(yStart - yCent, xStart - xCent);
                            if (startAngle < 0)
                            {
                                startAngle += 2.0 * M_PI; // Have to work entirely with angles between 0 and 2*pi
                            }
                            double endAngle = atan2(yEnd - yCent, xEnd - xCent);
                            if (endAngle < 0)
                            {
                                endAngle += 2.0 * M_PI; // Have to work entirely with angles between 0 and 2*pi
                            }
                            double arcAngle = endAngle - startAngle; // Difference of angles CCW

                            // Calculate points along arc with irregular 24-gon approximation
                            size_t nArcPt = ceil(arcAngle / (M_PI / 12.));
                            vector<double> paths; // Initialize vector of path coordinates
                            paths.push_back(xStart); // x-coordinate of current point
                            paths.push_back(yStart); // y-coordinate of current point
                            for (size_t indi = 1; indi < nArcPt; indi++)
                            {
                                paths.push_back(xCent + arcRad * cos(+2.0 * M_PI * indi / 24 + startAngle)); // x-coordinate CCW
                                paths.push_back(yCent + arcRad * sin(+2.0 * M_PI * indi / 24 + startAngle)); // y-coordinate CCW
                            }
                            paths.push_back(xEnd); // x-coordinate of end point
                            paths.push_back(yEnd); // y-coordinate of end point

                            // Finish interpolation by updating records and state
                            cellDrill.paths.emplace_back(path(paths, 1, {}, pathType, width)); // Push new path to geometric cell
                            currentPt = { xEnd, yEnd }; // Update current point
                        }
                    }
                }
                // Switch graphics mode to simple drill cycle (starting with G-code "G05" in IPC-NC-349/Excellon Format 2 or G-code "G81" in Excellon Format 1/RS-274X)
                else if ((fileLine.compare(0, 3, "G05") == 0) || (fileLine.compare(0, 3, "G81") == 0))
                {
                    drillMode = true;
                }
                // Handle drill or route operations with current tool bit and settings starting with x-coordinate of end point
                else if (fileLine.compare(0, 1, "X") == 0)
                {
                    // Find delimiters
                    size_t indXEnd = fileLine.find("X"); // Ending x-coordinate delimiter
                    size_t indYEnd = fileLine.find("Y"); // Ending y-coordinate delimiter
                    size_t indIOff = fileLine.find("I"); // Arc offset distance in x-direction (Excellon only)
                    size_t indJOff = fileLine.find("J"); // Arc offset distance in y-direction (Excellon only)
                    size_t indARad = fileLine.find("A"); // Arc radius
                    size_t indComment = fileLine.find(";");

                    // Extract numbers
                    double xEnd = currentPt[0]; // Initialize ending point to current point
                    double yEnd = currentPt[1];
                    double iOff = 0.; // Initialize as zero offset from starting point to arc center
                    double jOff = 0.;
                    double aRad = 0.; // Initialize as zero arc radius
                    if ((indXEnd != string::npos) && (indYEnd != string::npos))
                    {
                        xEnd = stod(fileLine.substr(indXEnd + 1, indYEnd - indXEnd - 1)) * adbDrill.getdbUnits();
                        if (leadingZeros)
                        {
                            xEnd *= pow(10.0, (indYEnd - indXEnd) - intPart); // The number of digits given minus the expected integer part is the power of ten in the correction factor in leading zeros mode
                        }
                        else
                        {
                            xEnd *= pow(10.0, -fracPart); // Correction factor in trailing zeros mode is just the precision of the given number
                        }
                    }
                    else if ((indXEnd != string::npos) && (indYEnd == string::npos) && (indIOff != string::npos))
                    {
                        xEnd = stod(fileLine.substr(indXEnd + 1, indIOff - indXEnd - 1)) * adbDrill.getdbUnits();
                        if (leadingZeros)
                        {
                            xEnd *= pow(10.0, (indIOff - indXEnd) - intPart);
                        }
                        else
                        {
                            xEnd *= pow(10.0, -fracPart);
                        }
                    }
                    else
                    {
                        xEnd = stod(fileLine.substr(indXEnd + 1, indARad - indXEnd - 1)) * adbDrill.getdbUnits();
                        if (leadingZeros)
                        {
                            xEnd *= pow(10.0, (indARad - indXEnd) - intPart);
                        }
                        else
                        {
                            xEnd *= pow(10.0, -fracPart);
                        }
                    }
                    if ((indYEnd != string::npos) && (indIOff != string::npos))
                    {
                        yEnd = stod(fileLine.substr(indYEnd + 1, indIOff - indYEnd - 1)) * adbDrill.getdbUnits();
                        if (leadingZeros)
                        {
                            yEnd *= pow(10.0, (indIOff - indYEnd) - intPart);
                        }
                        else
                        {
                            yEnd *= pow(10.0, -fracPart);
                        }
                    }
                    else
                    {
                        yEnd = stod(fileLine.substr(indYEnd + 1, indARad - indYEnd - 1)) * adbDrill.getdbUnits();
                        if (leadingZeros)
                        {
                            yEnd *= pow(10.0, (indARad - indYEnd) - intPart);
                        }
                        else
                        {
                            yEnd *= pow(10.0, -fracPart);
                        }
                    }
                    if (indIOff != string::npos)
                    {
                        iOff = stod(fileLine.substr(indIOff + 1, indJOff - indIOff - 1)) * adbDrill.getdbUnits();
                        if (leadingZeros)
                        {
                            iOff *= pow(10.0, (indJOff - indIOff) - intPart);
                        }
                        else
                        {
                            iOff *= pow(10.0, -fracPart);
                        }
                    }
                    if (indJOff != string::npos)
                    {
                        jOff = stod(fileLine.substr(indJOff + 1, indComment)) * adbDrill.getdbUnits();
                        if (leadingZeros)
                        {
                            jOff *= pow(10.0, fileLine.substr(indJOff + 1, indComment).length() - intPart);
                        }
                        else
                        {
                            jOff *= pow(10.0, -fracPart);
                        }
                    }
                    if (indARad != string::npos)
                    {
                        aRad = stod(fileLine.substr(indARad + 1, indComment)) * adbDrill.getdbUnits();
                        if (leadingZeros)
                        {
                            aRad *= pow(10.0, fileLine.substr(indARad + 1, indComment).length() - intPart);
                        }
                        else
                        {
                            aRad *= pow(10.0, -fracPart);
                        }
                        defaultRadius = aRad; // Update the default radius
                    }

                    // Perform drill mode operation
                    if (drillMode)
                    {
                        // Position tool bit and update records with tool bit image
                        currentPt = { xEnd, yEnd }; // Update current point
                        cellDrill.boundaries.emplace_back(currentTool.drawAsBound(xEnd, yEnd)); // Push new polygon boundary to geometric cell
                    }
                    // Perform rout mode operation
                    else
                    {
                        if (graphicsMode == 0)
                        {
                            // Perform rapid positioning
                            currentPt = { xEnd, yEnd }; // Update current point
                        }
                        else if ((graphicsMode == 1) && routToolDown)
                        {
                            // Perform full rout linear interpolation operation
                            int pathType = 1; // Round ends on GDSII path element
                            double width = currentTool.getCircumDia(); // Width of path will be diameter of tool bit
                            cellDrill.paths.emplace_back(path({ currentPt[0], currentPt[1], xEnd, yEnd }, 1, {}, pathType, width)); // Push new path to geometric cell
                            currentPt = { xEnd, yEnd }; // Update current point
                        }
                        else if (((graphicsMode == 2) || (graphicsMode == 3)) && routToolDown)
                        {
                            // Interpolate operation (movement rate of 100 in./min at 100% feed rate by default)
                            int pathType = 1; // Round ends on GDSII path element
                            double width = currentTool.getCircumDia(); // Width of path will be diameter of tool bit

                            // Calculate center of circular arc, radius, and angle covered
                            double xStart = currentPt[0];
                            double yStart = currentPt[1];
                            double xCent = xStart; // Initialize to no offset from starting point
                            double yCent = yStart;
                            double arcRad = 0.0; // Initialize to zero arc radius to interpolate
                            if ((aRad != 0.) && (indIOff == string::npos) && (indJOff == string::npos))
                            {
                                arcRad = aRad; // Use the radius read from this line in the file
                                double aSemi = 0.5 * hypot(xEnd - xStart, yEnd - yStart); // Semidiagonal a (connecting arc points) of the rhombus connecting the start point, end point, and two possible circle centers
                                double bSemi = 0.0; // Semidiagonal b (connecting possible circle centers) of the same rhombus
                                if (arcRad > aSemi)
                                {
                                    bSemi = sqrt(pow(arcRad, 2.) - pow(aSemi, 2.)); // Valid radius
                                }
                                else
                                {
                                    arcRad = 2.0 * aSemi; // CNC-7 minimal adjusted radius
                                }
                                if (graphicsMode == 2)
                                {
                                    xCent += 0.5 * (xEnd - xStart) + bSemi / aSemi * 0.5 * (yEnd - yStart); // Center is start arc point + midpoint of arc points + scaled walk rotated -pi/2 rad
                                    yCent += 0.5 * (xEnd - xStart) - bSemi / aSemi * 0.5 * (xEnd - xStart);
                                }
                                else
                                {
                                    xCent += 0.5 * (xEnd - xStart) - bSemi / aSemi * 0.5 * (yEnd - yStart); // Center is start arc point + midpoint of arc points + scaled walk rotated +pi/2 rad
                                    yCent += 0.5 * (xEnd - xStart) + bSemi / aSemi * 0.5 * (xEnd - xStart);
                                }
                            }
                            else if ((aRad == 0.) && (indIOff != string::npos) && (indJOff != string::npos))
                            {
                                xCent -= iOff; // Use offset coord = arc start coord - arc center coord
                                yCent -= jOff;
                                arcRad = 0.5 * (hypot(xStart - xCent, yStart - yCent) + hypot(xEnd - xCent, yEnd - yCent)); // Arithmetic mean because rounding errors prevent perfect circle
                            }
                            else
                            {
                                arcRad = defaultRadius; // Use the radius from a previous operation
                                double aSemi = 0.5 * hypot(xEnd - xStart, yEnd - yStart); // Semidiagonal a (connecting arc points) of the rhombus connecting the start point, end point, and two possible circle centers
                                double bSemi = 0.0; // Semidiagonal b (connecting possible circle centers) of the same rhombus
                                if (arcRad > aSemi)
                                {
                                    bSemi = sqrt(pow(arcRad, 2.) - pow(aSemi, 2.)); // Valid radius
                                }
                                else
                                {
                                    arcRad = 2.0 * aSemi; // CNC-7 minimal adjusted radius
                                }
                                if (graphicsMode == 2)
                                {
                                    xCent += 0.5 * (xEnd - xStart) + bSemi / aSemi * 0.5 * (yEnd - yStart); // Center is start arc point + midpoint of arc points + scaled walk rotated -pi/2 rad
                                    yCent += 0.5 * (xEnd - xStart) - bSemi / aSemi * 0.5 * (xEnd - xStart);
                                }
                                else
                                {
                                    xCent += 0.5 * (xEnd - xStart) - bSemi / aSemi * 0.5 * (yEnd - yStart); // Center is start arc point + midpoint of arc points + scaled walk rotated +pi/2 rad
                                    yCent += 0.5 * (xEnd - xStart) + bSemi / aSemi * 0.5 * (xEnd - xStart);
                                }
                            }
                            double startAngle = atan2(yStart - yCent, xStart - xCent);
                            if (startAngle < 0)
                            {
                                startAngle += 2.0 * M_PI; // Have to work entirely with angles between 0 and 2*pi
                            }
                            double endAngle = atan2(yEnd - yCent, xEnd - xCent);
                            if (endAngle < 0)
                            {
                                endAngle += 2.0 * M_PI; // Have to work entirely with angles between 0 and 2*pi
                            }
                            double arcAngle = 0.0;
                            if (graphicsMode == 2)
                            {
                                arcAngle = startAngle - endAngle; // Difference of angles CW
                            }
                            else
                            {
                                arcAngle = endAngle - startAngle; // Difference of angles CCW
                            }

                            // Calculate points along arc with irregular 24-gon approximation
                            size_t nArcPt = ceil(arcAngle / (M_PI / 12.));
                            vector<double> paths; // Initialize vector of path coordinates
                            paths.push_back(xStart); // x-coordinate of current point
                            paths.push_back(yStart); // y-coordinate of current point
                            for (size_t indi = 1; indi < nArcPt; indi++)
                            {
                                if (graphicsMode == 2)
                                {
                                    paths.push_back(xCent + arcRad * cos(-2.0 * M_PI * indi / 24 + startAngle)); // x-coordinate CW
                                    paths.push_back(yCent + arcRad * sin(-2.0 * M_PI * indi / 24 + startAngle)); // y-coordinate CW
                                }
                                else
                                {
                                    paths.push_back(xCent + arcRad * cos(+2.0 * M_PI * indi / 24 + startAngle)); // x-coordinate CCW
                                    paths.push_back(yCent + arcRad * sin(+2.0 * M_PI * indi / 24 + startAngle)); // y-coordinate CCW
                                }
                            }
                            paths.push_back(xEnd); // x-coordinate of end point
                            paths.push_back(yEnd); // y-coordinate of end point

                            // Finish interpolation by updating records and state
                            cellDrill.paths.emplace_back(path(paths, 1, {}, pathType, width)); // Push new path to geometric cell
                            currentPt = { xEnd, yEnd }; // Update current point
                        }
                    }
                }
                // Handle drill or route operations with current tool bit and settings starting with y-coordinate of end point
                else if (fileLine.compare(0, 1, "Y") == 0)
                {
                    // Find delimiters
                    size_t indYEnd = fileLine.find("Y"); // Ending y-coordinate delimiter
                    size_t indIOff = fileLine.find("I"); // Arc offset distance in x-direction (Excellon only)
                    size_t indJOff = fileLine.find("J"); // Arc offset distance in y-direction (Excellon only)
                    size_t indARad = fileLine.find("A"); // Arc radius
                    size_t indComment = fileLine.find(";");

                    // Extract numbers
                    double xEnd = currentPt[0]; // Initialize ending point to current point
                    double yEnd = currentPt[1];
                    double iOff = 0.; // Initialize as zero offset from starting point to arc center
                    double jOff = 0.;
                    double aRad = 0.; // Initialize as zero arc radius
                    if ((indYEnd != string::npos) && (indIOff != string::npos))
                    {
                        yEnd = stod(fileLine.substr(indYEnd + 1, indIOff - indYEnd - 1)) * adbDrill.getdbUnits();
                        if (leadingZeros)
                        {
                            yEnd *= pow(10.0, (indIOff - indYEnd) - intPart);
                        }
                        else
                        {
                            yEnd *= pow(10.0, -fracPart);
                        }
                    }
                    else
                    {
                        yEnd = stod(fileLine.substr(indYEnd + 1, indARad - indYEnd - 1)) * adbDrill.getdbUnits();
                        if (leadingZeros)
                        {
                            yEnd *= pow(10.0, (indARad - indYEnd) - intPart);
                        }
                        else
                        {
                            yEnd *= pow(10.0, -fracPart);
                        }
                    }
                    if (indIOff != string::npos)
                    {
                        iOff = stod(fileLine.substr(indIOff + 1, indJOff - indIOff - 1)) * adbDrill.getdbUnits();
                        if (leadingZeros)
                        {
                            iOff *= pow(10.0, (indJOff - indIOff) - intPart);
                        }
                        else
                        {
                            iOff *= pow(10.0, -fracPart);
                        }
                    }
                    if (indJOff != string::npos)
                    {
                        jOff = stod(fileLine.substr(indJOff + 1, indComment)) * adbDrill.getdbUnits();
                        if (leadingZeros)
                        {
                            jOff *= pow(10.0, fileLine.substr(indJOff + 1, indComment).length() - intPart);
                        }
                        else
                        {
                            jOff *= pow(10.0, -fracPart);
                        }
                    }
                    if (indARad != string::npos)
                    {
                        aRad = stod(fileLine.substr(indARad + 1, indComment)) * adbDrill.getdbUnits();
                        if (leadingZeros)
                        {
                            aRad *= pow(10.0, fileLine.substr(indARad + 1, indComment).length() - intPart);
                        }
                        else
                        {
                            aRad *= pow(10.0, -fracPart);
                        }
                        defaultRadius = aRad; // Update the default radius
                    }

                    // Perform drill mode operation
                    if (drillMode)
                    {
                        // Position tool bit and update records with tool bit image
                        currentPt = { xEnd, yEnd }; // Update current point
                        cellDrill.boundaries.emplace_back(currentTool.drawAsBound(xEnd, yEnd)); // Push new polygon boundary to geometric cell
                    }
                    // Perform rout mode operation
                    else
                    {
                        if (graphicsMode == 0)
                        {
                            // Perform rapid positioning
                            currentPt = { xEnd, yEnd }; // Update current point
                        }
                        else if ((graphicsMode == 1) && routToolDown)
                        {
                            // Perform full rout linear interpolation operation
                            int pathType = 1; // Round ends on GDSII path element
                            double width = currentTool.getCircumDia(); // Width of path will be diameter of tool bit
                            cellDrill.paths.emplace_back(path({ currentPt[0], currentPt[1], xEnd, yEnd }, 1, {}, pathType, width)); // Push new path to geometric cell
                            currentPt = { xEnd, yEnd }; // Update current point
                        }
                        else if (((graphicsMode == 2) || (graphicsMode == 3)) && routToolDown)
                        {
                            // Interpolate operation (movement rate of 100 in./min at 100% feed rate by default)
                            int pathType = 1; // Round ends on GDSII path element
                            double width = currentTool.getCircumDia(); // Width of path will be diameter of tool bit

                            // Calculate center of circular arc, radius, and angle covered
                            double xStart = currentPt[0];
                            double yStart = currentPt[1];
                            double xCent = xStart; // Initialize to no offset from starting point
                            double yCent = yStart;
                            double arcRad = 0.0; // Initialize to zero arc radius to interpolate
                            if ((aRad != 0.) && (indIOff == string::npos) && (indJOff == string::npos))
                            {
                                arcRad = aRad; // Use the radius read from this line in the file
                                double aSemi = 0.5 * hypot(xEnd - xStart, yEnd - yStart); // Semidiagonal a (connecting arc points) of the rhombus connecting the start point, end point, and two possible circle centers
                                double bSemi = 0.0; // Semidiagonal b (connecting possible circle centers) of the same rhombus
                                if (arcRad > aSemi)
                                {
                                    bSemi = sqrt(pow(arcRad, 2.) - pow(aSemi, 2.)); // Valid radius
                                }
                                else
                                {
                                    arcRad = 2.0 * aSemi; // CNC-7 minimal adjusted radius
                                }
                                if (graphicsMode == 2)
                                {
                                    xCent += 0.5 * (xEnd - xStart) + bSemi / aSemi * 0.5 * (yEnd - yStart); // Center is start arc point + midpoint of arc points + scaled walk rotated -pi/2 rad
                                    yCent += 0.5 * (xEnd - xStart) - bSemi / aSemi * 0.5 * (xEnd - xStart);
                                }
                                else
                                {
                                    xCent += 0.5 * (xEnd - xStart) - bSemi / aSemi * 0.5 * (yEnd - yStart); // Center is start arc point + midpoint of arc points + scaled walk rotated +pi/2 rad
                                    yCent += 0.5 * (xEnd - xStart) + bSemi / aSemi * 0.5 * (xEnd - xStart);
                                }
                            }
                            else if ((aRad == 0.) && (indIOff != string::npos) && (indJOff != string::npos))
                            {
                                xCent -= iOff; // Use offset coord = arc start coord - arc center coord
                                yCent -= jOff;
                                arcRad = 0.5 * (hypot(xStart - xCent, yStart - yCent) + hypot(xEnd - xCent, yEnd - yCent)); // Arithmetic mean because rounding errors prevent perfect circle
                            }
                            else
                            {
                                arcRad = defaultRadius; // Use the radius from a previous operation
                                double aSemi = 0.5 * hypot(xEnd - xStart, yEnd - yStart); // Semidiagonal a (connecting arc points) of the rhombus connecting the start point, end point, and two possible circle centers
                                double bSemi = 0.0; // Semidiagonal b (connecting possible circle centers) of the same rhombus
                                if (arcRad > aSemi)
                                {
                                    bSemi = sqrt(pow(arcRad, 2.) - pow(aSemi, 2.)); // Valid radius
                                }
                                else
                                {
                                    arcRad = 2.0 * aSemi; // CNC-7 minimal adjusted radius
                                }
                                if (graphicsMode == 2)
                                {
                                    xCent += 0.5 * (xEnd - xStart) + bSemi / aSemi * 0.5 * (yEnd - yStart); // Center is start arc point + midpoint of arc points + scaled walk rotated -pi/2 rad
                                    yCent += 0.5 * (xEnd - xStart) - bSemi / aSemi * 0.5 * (xEnd - xStart);
                                }
                                else
                                {
                                    xCent += 0.5 * (xEnd - xStart) - bSemi / aSemi * 0.5 * (yEnd - yStart); // Center is start arc point + midpoint of arc points + scaled walk rotated +pi/2 rad
                                    yCent += 0.5 * (xEnd - xStart) + bSemi / aSemi * 0.5 * (xEnd - xStart);
                                }
                            }
                            double startAngle = atan2(yStart - yCent, xStart - xCent);
                            if (startAngle < 0)
                            {
                                startAngle += 2.0 * M_PI; // Have to work entirely with angles between 0 and 2*pi
                            }
                            double endAngle = atan2(yEnd - yCent, xEnd - xCent);
                            if (endAngle < 0)
                            {
                                endAngle += 2.0 * M_PI; // Have to work entirely with angles between 0 and 2*pi
                            }
                            double arcAngle = 0.0;
                            if (graphicsMode == 2)
                            {
                                arcAngle = startAngle - endAngle; // Difference of angles CW
                            }
                            else
                            {
                                arcAngle = endAngle - startAngle; // Difference of angles CCW
                            }

                            // Calculate points along arc with irregular 24-gon approximation
                            size_t nArcPt = ceil(arcAngle / (M_PI / 12.));
                            vector<double> paths; // Initialize vector of path coordinates
                            paths.push_back(xStart); // x-coordinate of current point
                            paths.push_back(yStart); // y-coordinate of current point
                            for (size_t indi = 1; indi < nArcPt; indi++)
                            {
                                if (graphicsMode == 2)
                                {
                                    paths.push_back(xCent + arcRad * cos(-2.0 * M_PI * indi / 24 + startAngle)); // x-coordinate CW
                                    paths.push_back(yCent + arcRad * sin(-2.0 * M_PI * indi / 24 + startAngle)); // y-coordinate CW
                                }
                                else
                                {
                                    paths.push_back(xCent + arcRad * cos(+2.0 * M_PI * indi / 24 + startAngle)); // x-coordinate CCW
                                    paths.push_back(yCent + arcRad * sin(+2.0 * M_PI * indi / 24 + startAngle)); // y-coordinate CCW
                                }
                            }
                            paths.push_back(xEnd); // x-coordinate of end point
                            paths.push_back(yEnd); // y-coordinate of end point

                            // Finish interpolation by updating records and state
                            cellDrill.paths.emplace_back(path(paths, 1, {}, pathType, width)); // Push new path to geometric cell
                            currentPt = { xEnd, yEnd }; // Update current point
                        }
                    }
                }
                // Repeat drill operations with given separation (starting with command "R" in Excellon)
                else if (fileLine.compare(0, 1, "R") == 0)
                {
                    // Find delimiters
                    size_t indRepN = fileLine.find("R"); // Number of repititions after the first
                    size_t indXSep = fileLine.find("X"); // Separation x-coordinate delimiter
                    size_t indYSep = fileLine.find("Y"); // Separation y-coordinate delimiter
                    size_t indComment = fileLine.find(";");

                    // Extract numbers
                    int nRep = 0; // Initialize to zero repititions
                    double xSep = 0.0; // Initialize separation amount to zero
                    double ySep = 0.0;
                    if ((indRepN != string::npos) && (indXSep != string::npos))
                    {
                        nRep = stoi(fileLine.substr(indRepN + 1, indXSep - indRepN - 1));
                    }
                    else if ((indRepN != string::npos) && (indXSep == string::npos) && (indYSep != string::npos))
                    {
                        nRep = stoi(fileLine.substr(indRepN + 1, indYSep - indRepN - 1));
                    }
                    if ((indXSep != string::npos) && (indYSep != string::npos))
                    {
                        xSep = stod(fileLine.substr(indXSep + 1, indYSep - indXSep - 1)) * adbDrill.getdbUnits();
                        if (leadingZeros)
                        {
                            xSep *= pow(10.0, (indYSep - indXSep) - intPart); // The number of digits given minus the expected integer part is the power of ten in the correction factor in leading zeros mode
                        }
                        else
                        {
                            xSep *= pow(10.0, -fracPart); // Correction factor in trailing zeros mode is just the precision of the given number
                        }
                    }
                    else if ((indXSep != string::npos) && (indYSep == string::npos))
                    {
                        xSep = stod(fileLine.substr(indXSep + 1, indComment)) * adbDrill.getdbUnits();
                        if (leadingZeros)
                        {
                            xSep *= pow(10.0, fileLine.substr(indXSep + 1, indComment).length() - intPart);
                        }
                        else
                        {
                            xSep *= pow(10.0, -fracPart);
                        }
                    }
                    if (indYSep != string::npos)
                    {
                        ySep = stod(fileLine.substr(indYSep + 1, indComment)) * adbDrill.getdbUnits();
                        if (leadingZeros)
                        {
                            ySep *= pow(10.0, fileLine.substr(indYSep + 1, indComment).length() - intPart);
                        }
                        else
                        {
                            ySep *= pow(10.0, -fracPart);
                        }
                    }

                    // Repeat drill mode operations
                    if (drillMode)
                    {
                        // Position tool bit and update records with tool bit image repeatedly
                        for (size_t indRep = 0; indRep < nRep; indRep++)
                        {
                            currentPt = { currentPt[0] + xSep, currentPt[1] + ySep }; // Update current point
                            cellDrill.boundaries.emplace_back(currentTool.drawAsBound(currentPt[0], currentPt[1])); // Push new polygon boundary to geometric cell
                        }
                    }
                }
                // Move tool bit down for routing (starting with command "M15" in Excellon)
                else if (fileLine.compare(0, 3, "M15") == 0)
                {
                    routToolDown = true;
                }
                // Move tool bit up to stop routing (starting with command "M16" in Excellon)
                else if (fileLine.compare(0, 3, "M16") == 0)
                {
                    routToolDown = false;
                }
                // End of file rewind (starting with command "M30" in IPC-NC-349)
                else if (fileLine.compare(0, 3, "M30") == 0)
                {
                    break;
                }

                // Keep reading new lines in file, skipping comments
                getline(drillFile, fileLine);
                while (fileLine.compare(0, 1, ";") == 0)
                {
                    getline(drillFile, fileLine);
                }
            }

            // Close file
            drillFile.close();

            // Update ASCII database of drill holes
            adbDrill.setLibName("drill");
            adbDrill.appendCell(cellDrill);
            adbDrill.setdbUnits(adbDrill.getdbUnits() * 1.); // Rescale Gerber file units 1x to allow integer representation in GDSII

            // Print and dump the ASCII database of drill holes
            //adbDrill.print({ });
            adbDrill.dump();

            // Return ASCII database of drill holes
            return adbDrill;
        }
        else
        {
            // File could not be opened
            cerr << "Unable to open Excellon NC drill file" << endl;
            return AsciiDataBase();
        }
    }

    // Read interconnect modeling platform (IMP) file data into the solver database
    bool readIMPwriteGDSII(std::string impFileName, std::string gdsiiFileName)
    {
        // Attempt to open .imp file
        ifstream impFile(impFileName.c_str());
        if (impFile.is_open())
        {
            // Time manipulations
            char timeStr[80];                                                             // Character array to hold formatted time
            time_t rawtime;                                                               // Timer variable
            strftime(timeStr, sizeof(timeStr), "%d-%m-%Y %H:%M:%S", localtime(&rawtime)); // Use local timezone to format string
            std::string time(timeStr);                                                    // Formatted time to string parametrized constructor

            // Build single geometric cell from limboint.h to store information
            GeoCell cellIMP;
            size_t indPathSlash = impFileName.find_last_of("/");
            size_t indPathExten = impFileName.find_last_of(".");
            std::string cellName;
            if (indPathSlash == string::npos)
            {
                // No slashes found in impFileName
                cellName = impFileName.substr(0, indPathExten);
            }
            else
            {
                // Path for impFileName contains directories
                cellName = impFileName.substr(indPathSlash + 1, indPathExten - indPathSlash - 1);
            }
            cellIMP.cellName = cellName;
            cellIMP.dateCreate = time;
            cellIMP.dateMod = time;

            // Build ASCII database from limboint.h
            AsciiDataBase adbIMP;
            adbIMP.setFileName(gdsiiFileName);
            adbIMP.setDateMod(time);
            adbIMP.setDateAccess(time);

            // File is readable line-by-line
            std::string fileLine;
            getline(impFile, fileLine);

            // Save opening lines as metadata (mostly ignored right now)
            if (fileLine.compare(0, 7, "IMAP3D ") == 0)
            {
                // Save interconnect modeling platform version number
                std::string version = fileLine.substr(7, fileLine.length() - 7); // 7 characters in header syntax
            }

            // Initialize file-scope variables
            static double stripLength = 0;       // Initialize the strip length
            static int maxGDSIILayer = 0;        // Maximum GDSII layer number encountered
            static int numFreqPts = 0;           // Number of points in frequency sweep
            static vector<double> freqList = {}; // List of frequencies in sweep

            // Read rest of file line-by-line
            while (!impFile.eof())
            {
                // Handle units
                if (fileLine.compare(0, 12, "LINEARUNITS ") == 0)
                {
                    // Extract unit text
                    std::string units = fileLine.substr(12, fileLine.length() - 13);

                    // Find SI multiplier for given units
                    double multSI = 1.0;
                    if (units.compare("ym") == 0)
                    {
                        multSI = 1.e-24;
                    }
                    else if (units.compare("zm") == 0)
                    {
                        multSI = 1.e-21;
                    }
                    else if (units.compare("am") == 0)
                    {
                        multSI = 1.e-18;
                    }
                    else if (units.compare("fm") == 0)
                    {
                        multSI = 1.e-15;
                    }
                    else if (units.compare("pm") == 0)
                    {
                        multSI = 1.e-12;
                    }
                    else if (units.compare("nm") == 0)
                    {
                        multSI = 1.e-09;
                    }
                    else if (units.compare("um") == 0)
                    {
                        multSI = 1.e-06;
                    }
                    else if (units.compare("mm") == 0)
                    {
                        multSI = 1.e-03;
                    }
                    else if (units.compare("cm") == 0)
                    {
                        multSI = 1.e-02;
                    }
                    else if (units.compare("dm") == 0)
                    {
                        multSI = 1.e-01;
                    }

                    // Propagate units information to ASCII database now
                    adbIMP.setdbUserUnits(1.);
                    adbIMP.setdbUnits(multSI);
                }
                // Handle design name and skip to analysis parameters
                else if (fileLine.compare(0, 5, "NAME ") == 0)
                {
                    this->designName = fileLine.substr(5, fileLine.length() - 6);

                    // Save file position and jump ahead to analysis section
                    int savedFilePos = impFile.tellg();
                    while (!impFile.eof())
                    {
                        // Keep reading new lines in the file
                        getline(impFile, fileLine);

                        // Handle frequency information
                        if (fileLine.compare(0, 9, "Frequency") == 0)
                        {
                            // Find frequency sweep delimiters
                            size_t indBegin = fileLine.find("begin=");
                            size_t indEnd = fileLine.find("end=");
                            size_t indNoP = fileLine.find("numberofpoints=");

                            // Save frequency sweep information to variables
                            double freqBegin = stod(fileLine.substr(indBegin + 6, indEnd - indBegin - 7)); // Length is index difference minus space
                            double freqEnd = stod(fileLine.substr(indEnd + 4, indNoP - indEnd - 5));
                            numFreqPts = stoi(fileLine.substr(indNoP + 15));
                            if (numFreqPts == 1)
                            {
                                freqList.push_back(freqBegin);
                            }
                            else if (numFreqPts == 2)
                            {
                                freqList.push_back(freqBegin);
                                freqList.push_back(freqEnd);
                            }
                            else
                            {
                                double exp10Step = log10(freqEnd / freqBegin) / (numFreqPts - 1);
                                freqList.push_back(freqBegin);
                                for (size_t indi = 1; indi < numFreqPts - 1; indi++)
                                {
                                    freqList.push_back(freqList.back() * pow(10, exp10Step));
                                }
                                freqList.push_back(freqEnd); // Ensure last frequency is exact
                            }

                            // Wait to push back simulation settings after conductors are saved (need extrema of coordinates)
                        }

                        // Handle length in this weird spot
                        if (fileLine.compare(0, 6, "Length") == 0)
                        {
                            // Finally can save stripline length
                            stripLength = stod(fileLine.substr(7)) * adbIMP.getdbUnits();
                        }
                    }

                    // Jump back to line after name section
                    impFile.clear(); // Clear the eofbit and goodbit flags
                    impFile.seekg(savedFilePos);
                    getline(impFile, fileLine);
                }
                // Handle layer stack
                if (fileLine.compare(0, 5, "STACK") == 0)
                {
                    // Move down one line
                    getline(impFile, fileLine);

                    // Keep reading until end of layer stack
                    while ((fileLine.compare(0, 10, "CONDUCTORS") != 0) && (fileLine.compare(0, 8, "BOUNDARY") != 0))
                    {
                        // Record each layer
                        if (fileLine.length() >= 3)
                        {
                            // Find layer property delimiters
                            size_t indLayNam = fileLine.find(" ");
                            size_t indZStart = fileLine.find("z=");
                            size_t indHeight = fileLine.find("h=");
                            size_t indPermit = fileLine.find("e=");
                            size_t indLosTan = fileLine.find("TanD=");
                            size_t indConduc = fileLine.find("sigma=");

                            // Save layer information to variables
                            std::string layerName = fileLine.substr(0, indLayNam);
                            /*int gdsiiNum = -1;
                            if (layerName.find('M') != string::npos)
                            {
                                size_t indNumber = layerName.find("M");
                                gdsiiNum = stoi(layerName.substr(indNumber + 1, layerName.length() - indNumber - 1));
                                if (gdsiiNum > maxGDSIILayer)
                                {
                                    maxGDSIILayer = gdsiiNum;
                                }
                            }*/
                            int gdsiiNum = -1;
                            if (this->layers.size() == 0)
                            {
                                gdsiiNum = 0; // Start assigning layers from layer 0
                            }
                            else
                            {
                                gdsiiNum = this->layers.back().getGDSIINum() + 1; // Assign subsequent layers in order
                            }
                            if (gdsiiNum > maxGDSIILayer)
                            {
                                maxGDSIILayer = gdsiiNum;
                            }
                            double zStart = 0.;
                            if (indZStart != string::npos)
                            {
                                zStart = stod(fileLine.substr(indZStart + 2, indHeight - indZStart - 3)) * adbIMP.getdbUnits();
                            }
                            double zHeight = stod(fileLine.substr(indHeight + 2, fileLine.find(" ", indHeight) - indHeight - 2)) * adbIMP.getdbUnits();
                            double epsilon_r = 1.;
                            if (indPermit != string::npos)
                            {
                                epsilon_r = stod(fileLine.substr(indPermit + 2, fileLine.find(" ", indPermit) - indPermit - 2));
                            }
                            double lossTan = 0.;
                            if (indLosTan != string::npos) // See if it handles being at the end of line
                            {
                                lossTan = stod(fileLine.substr(indLosTan + 5, fileLine.find(" ", indLosTan) - indLosTan - 5));
                            }
                            double sigma = 0.;
                            if (indConduc != string::npos) // See if it handles being at the end of line
                            {
                                sigma = stod(fileLine.substr(indConduc + 6, fileLine.find(" ", indConduc) - indConduc - 6));
                            }

                            // Push new layer to class vector
                            (this->layers).emplace_back(Layer(layerName, gdsiiNum, zStart, zHeight, epsilon_r, lossTan, sigma));

                            // Put text box on GDSII layer
                            cellIMP.textboxes.emplace_back(textbox({ 0., 0. }, gdsiiNum, { }, 0, 0, { 1, 1 }, -10., strans(), layerName)); // Absolute width of 10 pixels
                        }
                        // Keep moving down the layer stack
                        getline(impFile, fileLine);
                    }
                }
                // Handle conductors
                if (fileLine.compare(0, 10, "CONDUCTORS") == 0)
                {
                    // Move down one line
                    getline(impFile, fileLine);

                    // Keep reading until end of conductor list
                    int gdsiiNum = 1; // Layer number of target GDSII file
                    double xmin = 0.;
                    double xmax = 0.;
                    double ymin = 0.;
                    double ymax = 0.;
                    while ((fileLine.compare(0, 8, "BOUNDARY") != 0) && (fileLine.compare(0, 9, "PORTTABLE") != 0))
                    {
                        // Record each conductor as a box
                        if (fileLine.length() >= 3)
                        {
                            // Find conductor property delimiters
                            size_t indCategory = fileLine.find(" ", 0);
                            size_t indCondName = fileLine.find(" ", indCategory);
                            size_t indX1 = fileLine.find("x1=", indCondName);
                            size_t indY1 = fileLine.find("y1=", indCondName);
                            size_t indZ1 = fileLine.find("z1=", indCondName);
                            size_t indX2 = fileLine.find("x2=", indCondName);
                            size_t indY2 = fileLine.find("y2=", indCondName);
                            size_t indZ2 = fileLine.find("z2=", indCondName);
                            size_t indSigma = fileLine.find("sigma=", indCondName);
                            size_t indLayer = fileLine.find("layer=", indCondName);
                            size_t indGroup = fileLine.find("group=", indCondName);

                            // Save conductor information to variables
                            std::string category = fileLine.substr(0, indCategory);
                            std::string condName = fileLine.substr(indCategory + 1, indCondName - indCategory - 1);
                            double x1 = stod(fileLine.substr(indX1 + 3, indY1 - indX1 - 4)) * adbIMP.getdbUnits(); // Length is index difference minus space
                            double y1 = stod(fileLine.substr(indY1 + 3, indZ1 - indY1 - 4)) * adbIMP.getdbUnits();
                            double x2 = stod(fileLine.substr(indX2 + 3, indY2 - indX2 - 4)) * adbIMP.getdbUnits();
                            double y2 = stod(fileLine.substr(indY2 + 3, indZ2 - indY2 - 4)) * adbIMP.getdbUnits();
                            std::string sigma = fileLine.substr(indSigma, indLayer - indSigma - 1);
                            std::string group;
                            if (indGroup != string::npos)
                            {
                                group = fileLine.substr(indGroup + 6);
                            }

                            // Check for extrema of coordinates
                            if (x1 < xmin)
                            {
                                xmin = x1;
                            }
                            if (x2 > xmax)
                            {
                                xmax = x2;
                            }
                            if (y1 < ymin)
                            {
                                ymin = y1;
                            }
                            if (y2 > ymax)
                            {
                                ymax = y2;
                            }

                            // Assign layer number based on layer stack-up
                            size_t indThisLayer = this->locateLayerName(fileLine.substr(indLayer + 6, fileLine.find(" ", indLayer)));
                            if (indThisLayer < (this->layers).size())
                            {
                                // Layer name for conductor matched name in layer stack-up
                                gdsiiNum = ((this->layers)[indThisLayer]).getGDSIINum();
                            }

                            // Push new box to the geometric cell
                            cellIMP.boxes.emplace_back(box({x2, y1, x2, y2 + stripLength, x1, y2 + stripLength, x1, y1, x2, y1}, gdsiiNum, {sigma, condName, category, group}, 0));
                        }
                        // Keep moving down the conductor list
                        getline(impFile, fileLine);
                    }

                    // Add on planes
                    size_t indGroundPlane = this->locateLayerName("GroundPlane"); // Start with ground (bottom) plane
                    ((this->layers)[indGroundPlane]).setGDSIINum(0);              // Assign ground plane to layer 0
                    vector<std::string> propGround;                               // Ground plane properties
                    propGround.push_back("sigma=" + to_string(((this->layers)[indGroundPlane]).getSigma()));
                    propGround.push_back("GroundPlane");
                    propGround.push_back("plane");
                    propGround.push_back("");
                    cellIMP.boxes.emplace_back(box({xmax, ymin, xmax, 2 * ymax + stripLength, xmin, 2 * ymax + stripLength, xmin, ymin, xmax, ymin}, ((this->layers)[indGroundPlane]).getGDSIINum(), propGround, 0));
                    size_t indTopPlane = this->locateLayerName("TopPlane");     // Next is top plane
                    ((this->layers)[indTopPlane]).setGDSIINum(++maxGDSIILayer); // Assign top plane to layer one more than maximum metallic layer
                    vector<std::string> propTop;                                // Top plane properties
                    propTop.push_back("sigma=" + to_string(((this->layers)[indTopPlane]).getSigma()));
                    propTop.push_back("TopPlane");
                    propTop.push_back("plane");
                    propTop.push_back("");
                    cellIMP.boxes.emplace_back(box({xmax, ymin, xmax, 2 * ymax + stripLength, xmin, 2 * ymax + stripLength, xmin, ymin, xmax, ymin}, ((this->layers)[indTopPlane]).getGDSIINum(), propTop, 0));

                    // Save simulation settings at last
                    this->settings = SimSettings(adbIMP.getdbUnits(), {xmin, xmax, ymin, 2 * ymax + stripLength, (this->layers).back().getZStart(), (this->layers).front().getZStart() + (this->layers).front().getZHeight()}, 1.0, 0.0, freqList);
                }
                // Handle port table
                if (fileLine.compare(0, 9, "PORTTABLE") == 0)
                {
                    // Move down one line
                    getline(impFile, fileLine);

                    // Keep reading until end of port table
                    vector<Port> ports;
                    while ((fileLine.compare(0, 8, "ANALYSIS") != 0) && !impFile.eof())
                    {
                        if (fileLine.length() >= 3)
                        {
                            // Find port table delimiters
                            size_t indNameStart = fileLine.find("group=", 0) + 6;
                            size_t indNameEnd = fileLine.find(" ", indNameStart);
                            size_t indZNearStart = fileLine.find("znear=", indNameEnd) + 6;
                            size_t indZNearEnd = fileLine.find(" ", indZNearStart);
                            size_t indZFar = fileLine.find("zfar=", indNameEnd) + 5;

                            // Save port table information to variables
                            std::string groupName = fileLine.substr(indNameStart, indNameEnd - indNameStart);
                            double Z_near = stod(fileLine.substr(indZNearStart, indZNearStart - indZNearEnd));
                            double Z_far = stod(fileLine.substr(indZFar));

                            // Register a new port in vectors
                            vector<double> portSupRet;
                            size_t indCond, indLayer;
                            for (indCond = 0; indCond < cellIMP.boxes.size(); indCond++)
                            {
                                if (groupName.compare(((cellIMP.boxes[indCond]).getProps())[1]) == 0)
                                {
                                    break;
                                }
                            }
                            if (indCond >= cellIMP.boxes.size())
                            {
                                portSupRet = {0., 0., 0., 0., 0., 0.}; // Not found, default has supply and return at origin
                            }
                            else
                            {
                                vector<double> boxCoord = (cellIMP.boxes[indCond]).getBoxes();                                               // Coordinates of box on layer
                                indLayer = this->locateLayerGDSII((cellIMP.boxes[indCond]).getLayer());                                      // Index of layer in structure field
                                portSupRet = {boxCoord[0], boxCoord[1], (this->layers)[indLayer].getZStart(), boxCoord[0], boxCoord[1], 0.}; // xsup = LR xcoord, ysup = LR ycoord, zsup = layer zStart, xret = xsup, yret = ysup, zret = 0.
                            }
                            ports.emplace_back(Port(groupName, 'B', Z_near, 1, portSupRet, (this->layers)[indLayer].getGDSIINum())); // Assume port has unit multiplicity
                        }
                        // Keep moving down the port table
                        getline(impFile, fileLine);
                    }

                    // Propagate port information to Solver Database now
                    this->para = Parasitics(ports, dMat(), dMat(), freqList);
                }

                // Keep reading new lines in file
                getline(impFile, fileLine);
            }

            // Close file
            impFile.close();

            // Update ASCII database
            adbIMP.setLibName(this->designName);
            adbIMP.appendCell(cellIMP);
            adbIMP.setdbUnits(adbIMP.getdbUnits() * 1.e-3); // Rescale .imp file units 0.001x to allow integer representation in GDSII

            // Print the ASCII database
            adbIMP.print({0});
            (this->settings).print();

            // Write GDSII file to hard drive
            bool dumpPassed = adbIMP.dump();
            return dumpPassed;
        }
        else
        {
            // File could not be opened
            return false;
        }
    }

    // Load simulation input file
    bool readSimInput(std::string inputFileName)
    {
        // Attempt to open simulation input file
        ifstream inputFile(inputFileName.c_str());
        if (inputFile.is_open())
        {
            // File is readable line-by-line
            std::string fileLine;
            getline(inputFile, fileLine);

            // Read rest of file line-by-line
            while (!inputFile.eof())
            {
                // Handle total size
                if (fileLine.compare(0, 10, "TOTAL SIZE") == 0)
                {
                    // Move down one line, skipping comments
                    getline(inputFile, fileLine);
                    while (fileLine.compare(0, 1, "#") == 0)
                    {
                        getline(inputFile, fileLine);
                    }

                    // Design limits information recorded differently based on how many numbers appear
                    std::string noComment = fileLine.substr(0, fileLine.find(" #"));
                    size_t nSpace = count(noComment.begin(), noComment.end(), ' ');

                    // Obtain limits of IC design size
                    double xmin, xmax, ymin, ymax, zmin, zmax;
                    if (nSpace >= 5)
                    {
                        // Conventional specification has six numbers separated by spaces
                        size_t indCoordStart = 0;
                        size_t indCoordEnd = fileLine.find(" ", indCoordStart);
                        xmin = stod(fileLine.substr(indCoordStart, indCoordEnd - indCoordStart));
                        indCoordStart = indCoordEnd + 1;
                        indCoordEnd = fileLine.find(" ", indCoordStart);
                        xmax = stod(fileLine.substr(indCoordStart, indCoordEnd - indCoordStart));
                        indCoordStart = indCoordEnd + 1;
                        indCoordEnd = fileLine.find(" ", indCoordStart);
                        ymin = stod(fileLine.substr(indCoordStart, indCoordEnd - indCoordStart));
                        indCoordStart = indCoordEnd + 1;
                        indCoordEnd = fileLine.find(" ", indCoordStart);
                        ymax = stod(fileLine.substr(indCoordStart, indCoordEnd - indCoordStart));
                        indCoordStart = indCoordEnd + 1;
                        indCoordEnd = fileLine.find(" ", indCoordStart);
                        zmin = stod(fileLine.substr(indCoordStart, indCoordEnd - indCoordStart));
                        indCoordStart = indCoordEnd + 1;
                        indCoordEnd = fileLine.find(" ", indCoordStart);
                        zmax = stod(fileLine.substr(indCoordStart, indCoordEnd - indCoordStart));
                    }
                    else
                    {
                        // Alternative specification has Gerber outline file name followed by two numbers
                        size_t indCoordStart = 0;
                        size_t indCoordEnd = fileLine.find(" ", indCoordStart);
                        xmin = NAN;
                        xmax = NAN;
                        ymin = NAN;
                        ymax = NAN;
                        std::string outlineFile = fileLine.substr(indCoordStart, indCoordEnd - indCoordStart);
                        std::string outlinePath = inputFileName.substr(0, inputFileName.find_last_of("/") + 1) + outlineFile; // Get path up to simulation input file for outline file path
                        vector<double> hullCoord = this->readGerberOutline(outlinePath); // Not doing anything with this data right now
                        //cout << "Number of convex hull coordinates: " << hullCoord.size() << endl;
                        indCoordStart = indCoordEnd + 1;
                        indCoordEnd = fileLine.find(" ", indCoordStart);
                        zmin = stod(fileLine.substr(indCoordStart, indCoordEnd - indCoordStart));
                        indCoordStart = indCoordEnd + 1;
                        indCoordEnd = fileLine.find(" ", indCoordStart);
                        zmax = stod(fileLine.substr(indCoordStart, indCoordEnd - indCoordStart));
                    }

                    // Move down one line, skipping comments
                    getline(inputFile, fileLine);
                    while (fileLine.compare(0, 1, "#") == 0)
                    {
                        getline(inputFile, fileLine);
                    }

                    // Obtain length unit
                    double lengthUnit = stod(fileLine.substr(fileLine.find("lengthUnit = ") + 13, fileLine.find(" #")));

                    // Create simulation settings
                    this->settings = SimSettings(lengthUnit, {xmin * lengthUnit, xmax * lengthUnit, ymin * lengthUnit, ymax * lengthUnit, zmin * lengthUnit, zmax * lengthUnit}, 1., 0., {});
                }
                // Handle frequency sweep parameters
                else if (fileLine.compare(0, 9, "FREQUENCY") == 0)
                {
                    // Move down one line, skipping comments
                    getline(inputFile, fileLine);
                    while (fileLine.compare(0, 1, "#") == 0)
                    {
                        getline(inputFile, fileLine);
                    }

                    // Obtain frequency unit
                    double freqUnit = stod(fileLine.substr(fileLine.find("freqUnit = ") + 11, fileLine.find(" #")));

                    // Move down one line, skipping comments
                    getline(inputFile, fileLine);
                    while (fileLine.compare(0, 1, "#") == 0)
                    {
                        getline(inputFile, fileLine);
                    }

                    // Obtain first frequency
                    double freqStart = stod(fileLine.substr(fileLine.find("freqStart = ") + 12, fileLine.find(" #")));

                    // Move down one line, skipping comments
                    getline(inputFile, fileLine);
                    while (fileLine.compare(0, 1, "#") == 0)
                    {
                        getline(inputFile, fileLine);
                    }

                    // Obtain last frequency
                    double freqEnd = stod(fileLine.substr(fileLine.find("freqEnd = ") + 10, fileLine.find(" #")));

                    // Move down one line, skipping comments
                    getline(inputFile, fileLine);
                    while (fileLine.compare(0, 1, "#") == 0)
                    {
                        getline(inputFile, fileLine);
                    }

                    // Obtain number of frequencies
                    size_t nFreq = stoi(fileLine.substr(fileLine.find("nfreq = ") + 8, fileLine.find(" #")));

                    // Move down one line, skipping comments
                    getline(inputFile, fileLine);
                    while (fileLine.compare(0, 1, "#") == 0)
                    {
                        getline(inputFile, fileLine);
                    }

                    // Obtain frequency scaling
                    double freqScale = stod(fileLine.substr(fileLine.find("freqScale = ") + 12, fileLine.find(" #")));

                    // Establish frequency list
                    vector<double> freqList;
                    if (nFreq == 1)
                    {
                        freqList.push_back(freqStart);
                    }
                    else if (nFreq == 2)
                    {
                        freqList.push_back(freqStart);
                        freqList.push_back(freqEnd);
                    }
                    else if ((nFreq > 2) && (freqScale == 1.)) // Linear interpolation of frequency sweep
                    {
                        double linStep = (freqEnd - freqStart) / (nFreq - 1);
                        freqList.push_back(freqStart);
                        for (size_t indi = 1; indi < nFreq - 1; indi++)
                        {
                            freqList.push_back(freqList.back() + linStep);
                        }
                        freqList.push_back(freqEnd); // Ensure last frequency is exact
                    }
                    else // Logarithmic interpolation of frequency sweep
                    {
                        double exp10Step = log10(freqEnd / freqStart) / (nFreq - 1);
                        freqList.push_back(freqStart);
                        for (size_t indi = 1; indi < nFreq - 1; indi++)
                        {
                            freqList.push_back(freqList.back() * pow(10, exp10Step));
                        }
                        freqList.push_back(freqEnd); // Ensure last frequency is exact
                    }

                    // Update simulation settings
                    (this->settings).setFreqUnit(freqUnit);
                    (this->settings).setFreqScale(freqScale);
                    (this->settings).setFreqs(freqList);
                }
                // Handle dielectric stack-up
                else if (fileLine.compare(0, 16, "DIELECTRIC STACK") == 0)
                {
                    // Move down one line, skipping comments
                    getline(inputFile, fileLine);
                    while (fileLine.compare(0, 1, "#") == 0)
                    {
                        getline(inputFile, fileLine);
                    }

                    // Obtain number of dieletric layers in stack-up
                    size_t numStack = 0;
                    if (fileLine.find("numStack = ") < string::npos)
                    {
                        numStack = stoi(fileLine.substr(fileLine.find("numStack = ") + 11, fileLine.find(" #")));
                    }
                    else if (fileLine.find("numLayer = ") < string::npos)
                    {
                        numStack = stoi(fileLine.substr(fileLine.find("numLayer = ") + 11, fileLine.find(" #")));
                    }

                    // Move down one line, skipping comments
                    getline(inputFile, fileLine);
                    while (fileLine.compare(0, 1, "#") == 0)
                    {
                        getline(inputFile, fileLine);
                    }

                    // Read each line in dielectric stack
                    for (size_t indStack = 0; indStack < numStack; indStack++)
                    {
                        // Get dielectric stack delimiters
                        size_t indNameEnd = fileLine.find(" ");
                        size_t indZStart = fileLine.find("z = ");
                        size_t indHeight = fileLine.find("h = ");
                        size_t indRelPermit = fileLine.find("e = ");
                        size_t indLossTan = fileLine.find("TanD = ");
                        size_t indConduc = fileLine.find("sigma = ");

                        // Save dielectric stack information to variables
                        std::string layerName = fileLine.substr(0, indNameEnd);
                        bool numberInName = true; // Assume GDSII number could be assigned from name
                        int layerNumGDSII = -1; // Default value for undescribed layers
                        if (layerName.find('M') != string::npos) // Give leeway for layer names that have a word, followed by 'M' for metal layer, and then the number
                        {
                            size_t indNumber = layerName.find("M");
                            for (size_t indChar = indNumber + 1; indChar < layerName.length(); indChar++)
                            {
                                if (!isdigit(layerName[indChar]))
                                {
                                    numberInName = false;
                                }
                            }
                            // Remaining layer name is an integer
                            if (numberInName)
                            {
                                layerNumGDSII = stoi(layerName.substr(indNumber + 1, layerName.length() - indNumber - 1));
                            }
                        }
                        else // The layer name might just be the number
                        {
                            for (size_t indChar = 0; indChar < layerName.length(); indChar++)
                            {
                                if (!isdigit(layerName[indChar]))
                                {
                                    numberInName = false;
                                }
                            }
                            // Whole layer name is an integer
                            if (numberInName)
                            {
                                layerNumGDSII = stoi(layerName);
                            }
                        }
                        double layerZStart = 0.; // Assume first layer has bottom at z_start = 0.0 unless specified
                        if (indZStart < string::npos) // Bottom z-coordinate of layer was given
                        {
                            layerZStart = stod(fileLine.substr(indZStart + 4, fileLine.find(" ", indZStart) - indZStart - 4)) * (this->settings).getLengthUnit();
                        }
                        else if ((indZStart == string::npos) || (indStack > 0)) // No bottom z-coordinate given, so assume it builds up where previous layer ends
                        {
                            layerZStart = (this->layers).back().getZStart() + (this->layers).back().getZHeight();
                        }
                        double layerHeight = stod(fileLine.substr(indHeight + 4, fileLine.find(" ", indHeight) - indHeight - 4)) * (this->settings).getLengthUnit();
                        double layerEpsilonR = stod(fileLine.substr(indRelPermit + 4, fileLine.find(" ", indRelPermit) - indRelPermit - 4));
                        double layerLossTan = 0.;
                        if (indLossTan < string::npos) // Loss tangent was given
                        {
                            layerLossTan = stod(fileLine.substr(indLossTan + 7, fileLine.find(" ", indLossTan) - indLossTan - 7));
                        }
                        double layerSigma = 0.;
                        if (indConduc < string::npos) // Conductivity inside layer was given
                        {
                            layerSigma = stod(fileLine.substr(indConduc + 8, fileLine.find(" ", indConduc) - indConduc - 8));
                        }

                        // Register a new layer in layers field
                        (this->layers).emplace_back(Layer(layerName, layerNumGDSII, layerZStart, layerHeight, layerEpsilonR, layerLossTan, layerSigma));

                        // Keep moving down the dielectric stack, skipping comments
                        getline(inputFile, fileLine);
                        while (fileLine.compare(0, 1, "#") == 0)
                        {
                            getline(inputFile, fileLine);
                        }
                    }
                }
                // Handle port list if not already populated by GDSII file
                else if ((fileLine.compare(0, 4, "PORT") == 0) && ((this->para).getPorts().size() == 0))
                {
                    // Move down one line, skipping comments
                    getline(inputFile, fileLine);
                    while (fileLine.compare(0, 1, "#") == 0)
                    {
                        getline(inputFile, fileLine);
                    }

                    // Obtain number of ports in list
                    size_t numPort = 0;
                    if (fileLine.find("numPorts = ") < string::npos)
                    {
                        numPort = stoi(fileLine.substr(fileLine.find("numPorts = ") + 11, fileLine.find(" #")));
                    }
                    else if (fileLine.find("numPort = ") < string::npos)
                    {
                        numPort = stoi(fileLine.substr(fileLine.find("numPort = ") + 10, fileLine.find(" #")));
                    }

                    // Move down one line, skipping comments
                    getline(inputFile, fileLine);
                    while (fileLine.compare(0, 1, "#") == 0)
                    {
                        getline(inputFile, fileLine);
                    }

                    // Read each line in the port list
                    vector<Port> ports;
                    size_t indPort = 0; // Initialize port index
                    while ((fileLine.length() >= 3) && !(inputFile.eof()))
                    {
                        // Port information recorded differently based on how many numbers appear
                        std::string noComment = fileLine.substr(0, fileLine.find(" #"));
                        size_t nSpace = count(noComment.begin(), noComment.end(), ' ');

                        // Obtain limits of IC design size
                        std::string portName; // Placeholder for port name
                        double xsup, ysup, zsup, xret, yret, zret; // Coordinates of a port side (m)
                        int multiplicity = 1; // Number of simultaneous excitations
                        int sourceDir = 0;
                        char portDir;
                        int portLayer = -1;
                        if (nSpace == 4)
                        {
                            portName = "port" + to_string(indPort + 1); // Port has no official name
                            size_t indCoordStart = 0;
                            size_t indCoordEnd = fileLine.find(" ", indCoordStart);
                            xsup = stod(fileLine.substr(indCoordStart, indCoordEnd - indCoordStart)) * (this->settings).getLengthUnit();
                            indCoordStart = indCoordEnd + 1;
                            indCoordEnd = fileLine.find(" ", indCoordStart);
                            ysup = stod(fileLine.substr(indCoordStart, indCoordEnd - indCoordStart)) * (this->settings).getLengthUnit();
                            indCoordStart = indCoordEnd + 1;
                            indCoordEnd = fileLine.find(" ", indCoordStart);
                            xret = stod(fileLine.substr(indCoordStart, indCoordEnd - indCoordStart)) * (this->settings).getLengthUnit();
                            indCoordStart = indCoordEnd + 1;
                            indCoordEnd = fileLine.find(" ", indCoordStart);
                            yret = stod(fileLine.substr(indCoordStart, indCoordEnd - indCoordStart)) * (this->settings).getLengthUnit();
                            indCoordStart = indCoordEnd + 1;
                            indCoordEnd = fileLine.find(" ", indCoordStart);
                            portLayer = stoi(fileLine.substr(indCoordStart, indCoordEnd - indCoordStart)); // Neglect comments
                            sourceDir = 0;                                                                 // No soure direction information included

                            zsup = (this->layers)[this->locateLayerGDSII(portLayer)].getZStart();
                            zret = zsup + (this->layers)[this->locateLayerGDSII(portLayer)].getZHeight(); // Return assumed to be on same layer but on upper end
                        }
                        else if (nSpace == 6)
                        {
                            portName = "port" + to_string(indPort + 1); // Port has no official name
                            size_t indCoordStart = 0;
                            size_t indCoordEnd = fileLine.find(" ", indCoordStart);
                            xsup = stod(fileLine.substr(indCoordStart, indCoordEnd - indCoordStart)) * (this->settings).getLengthUnit();
                            indCoordStart = indCoordEnd + 1;
                            indCoordEnd = fileLine.find(" ", indCoordStart);
                            ysup = stod(fileLine.substr(indCoordStart, indCoordEnd - indCoordStart)) * (this->settings).getLengthUnit();
                            indCoordStart = indCoordEnd + 1;
                            indCoordEnd = fileLine.find(" ", indCoordStart);
                            zsup = stod(fileLine.substr(indCoordStart, indCoordEnd - indCoordStart)) * (this->settings).getLengthUnit();
                            indCoordStart = indCoordEnd + 1;
                            indCoordEnd = fileLine.find(" ", indCoordStart);
                            xret = stod(fileLine.substr(indCoordStart, indCoordEnd - indCoordStart)) * (this->settings).getLengthUnit();
                            indCoordStart = indCoordEnd + 1;
                            indCoordEnd = fileLine.find(" ", indCoordStart);
                            yret = stod(fileLine.substr(indCoordStart, indCoordEnd - indCoordStart)) * (this->settings).getLengthUnit();
                            indCoordStart = indCoordEnd + 1;
                            indCoordEnd = fileLine.find(" ", indCoordStart);
                            zret = stod(fileLine.substr(indCoordStart, indCoordEnd - indCoordStart)) * (this->settings).getLengthUnit();
                            indCoordStart = indCoordEnd + 1;
                            indCoordEnd = fileLine.find(" ", indCoordStart);
                            sourceDir = stoi(fileLine.substr(indCoordStart, indCoordEnd - indCoordStart)); // Neglect comments
                            portLayer = (this->layers)[this->locateLayerZStart(zsup)].getGDSIINum();// Only use supply point to determine layer
                        }
                        else if (nSpace >= 6)
                        {
                            size_t indCoordStart = 0;
                            size_t indCoordEnd = fileLine.find(" ", indCoordStart);
                            portName = fileLine.substr(indCoordStart, indCoordEnd - indCoordStart); // Save the port name
                            indCoordStart = indCoordEnd + 1;
                            indCoordEnd = fileLine.find(" ", indCoordStart);
                            xsup = stod(fileLine.substr(indCoordStart, indCoordEnd - indCoordStart)) * (this->settings).getLengthUnit();
                            indCoordStart = indCoordEnd + 1;
                            indCoordEnd = fileLine.find(" ", indCoordStart);
                            ysup = stod(fileLine.substr(indCoordStart, indCoordEnd - indCoordStart)) * (this->settings).getLengthUnit();
                            indCoordStart = indCoordEnd + 1;
                            indCoordEnd = fileLine.find(" ", indCoordStart);
                            zsup = stod(fileLine.substr(indCoordStart, indCoordEnd - indCoordStart)) * (this->settings).getLengthUnit();
                            indCoordStart = indCoordEnd + 1;
                            indCoordEnd = fileLine.find(" ", indCoordStart);
                            xret = stod(fileLine.substr(indCoordStart, indCoordEnd - indCoordStart)) * (this->settings).getLengthUnit();
                            indCoordStart = indCoordEnd + 1;
                            indCoordEnd = fileLine.find(" ", indCoordStart);
                            yret = stod(fileLine.substr(indCoordStart, indCoordEnd - indCoordStart)) * (this->settings).getLengthUnit();
                            indCoordStart = indCoordEnd + 1;
                            indCoordEnd = fileLine.find(" ", indCoordStart);
                            zret = stod(fileLine.substr(indCoordStart, indCoordEnd - indCoordStart)) * (this->settings).getLengthUnit();
                            indCoordStart = indCoordEnd + 1;
                            indCoordEnd = fileLine.find(" ", indCoordStart);
                            sourceDir = stoi(fileLine.substr(indCoordStart, indCoordEnd - indCoordStart)); // Neglect comments
                            portLayer = (this->layers)[this->locateLayerZStart(zsup)].getGDSIINum(); // Only use supply point to determine layer
                        }

                        // Assign unbalanced port direction
                        if (sourceDir == -1)
                        {
                            portDir = 'O';
                        }
                        else if (sourceDir == +1)
                        {
                            portDir = 'I';
                        }
                        else
                        {
                            portDir = 'B'; // Treat as bidirectional if given or direction unclear
                        }

                        // Overwrite or append port information to vector
                        //cout << " Port side belongs to port: " << portName << endl;
                        bool portFound = false; // Does the port already exist?
                        vector<double> coord = { xsup, ysup, zsup, xret, yret, zret }; // Put coordinates into a vector (m)
                        if (ports.size() > 0) // Only check port side against previous port if previous ports exist
                        {
                            for (size_t indi = 0; indi < ports.size(); indi++)
                            {
                                //cout << "  Port side """ << portName << """ being compared with " << ports[indi].getPortName() << endl;
                                if (portName.compare(ports[indi].getPortName()) == 0)
                                {
                                    // Port found to exist already
                                    Port oldPort = ports[indi]; // Obtain copy of old port
                                    char newPortDir = oldPort.getPortDir(); // Determine effective port direction
                                    if ((portDir != newPortDir) && (portDir != 'B') && (newPortDir != 'B'))
                                    {
                                        newPortDir = 'B'; // Conflicting input/output sets port to bidirectional overall
                                    }
                                    else if ((portDir != newPortDir) && (portDir != 'B') && (newPortDir == 'B'))
                                    {
                                        newPortDir = portDir; // Previous bidirectional state replaced with direction from this line
                                    } // No work needed for agreement or if bidirectional state on this line would be replaced with old state anyway
                                    vector<double> newCoord = oldPort.getCoord();
                                    newCoord.insert(newCoord.end(), coord.begin(), coord.end()); // Append 6 more coordinates to the end of the coordinate list
                                    ports[indi] = Port(portName, newPortDir, 50., oldPort.getMultiplicity() + 1, newCoord, portLayer); // Overwrite port with updated multiplicity, assuming port on layer of final port side supply point
                                    portFound = true;
                                    break;
                                }
                            }
                        }
                        if (!portFound)
                        {
                            //cout << " Creating brand new port for " << portName << endl;
                            ports.emplace_back(Port(portName, portDir, 50., 1, coord, portLayer));
                        }

                        // Move down one line in port list, skipping comments
                        getline(inputFile, fileLine);
                        while ((fileLine.compare(0, 1, "#") == 0) && !(inputFile.eof()))
                        {
                            getline(inputFile, fileLine);
                        }
                    }

                    // Propagate port list to parasitics data structure now
                    this->para = Parasitics(ports, dMat(numPort, numPort), dMat(numPort, numPort), (this->settings).getFreqsHertz());
                }

                // Keep reading new lines in file
                getline(inputFile, fileLine);
            }

            // Close file
            inputFile.close();
            return true;
        }
        else
        {
            // File could not be opened
            return false;
        }
    }

    // Convert to fdtdMesh
    void convertToFDTDMesh(fdtdMesh *data, int numCdtRow, unordered_set<double> *portCoorx, unordered_set<double> *portCoory)
    {
        // Use the sole argument
        data->numCdtRow = numCdtRow;

        // Use simulation settings to set fields
        data->lengthUnit = (this->settings).getLengthUnit();
        data->freqUnit = (this->settings).getFreqUnit();
        data->nfreq = (this->settings).getNFreq();
        data->freqStart = (this->settings).getFreqs().front();
        data->freqEnd = (this->settings).getFreqs().back();
        data->freqScale = (this->settings).getFreqScale();
        data->xlim1 = (this->settings).getLimits()[0];
        data->xlim2 = (this->settings).getLimits()[1];
        data->ylim1 = (this->settings).getLimits()[2];
        data->ylim2 = (this->settings).getLimits()[3];
        data->zlim1 = (this->settings).getLimits()[4];
        data->zlim2 = (this->settings).getLimits()[5];

        // Use layer stack-up information to set fields
        vector<Layer> physicalLayers = this->getValidLayers(); // Work with only physically valid layers
        data->numStack = physicalLayers.size();
        for (size_t indLayer = 0; indLayer < data->numStack; indLayer++)
        {
            Layer thisLayer = physicalLayers[indLayer]; // Get copy of layer for this iteration
            data->stackEps.push_back(thisLayer.getEpsilonR()); // Relative permittivity of the layers
            data->stackSig.push_back(thisLayer.getSigma()); // (Real) conductivity of the conductors in the layers
            data->stackBegCoor.push_back(thisLayer.getZStart());
            data->stackEndCoor.push_back(thisLayer.getZStart() + thisLayer.getZHeight());
            data->stackName.push_back(thisLayer.getLayerName());
        }

        // Set conductor information from saved stack information
        data->stackCdtMark = (double*)malloc(sizeof(double) * data->numStack);
        for (size_t indi = 0; indi < data->numCdtRow; indi++)
        {
            for (size_t indj = 0; indj < data->numStack; indj++)
            {
                // See if conductor layer numbers being built correspond to GDSII layer number
                if (data->conductorIn[indi].layer == physicalLayers[indj].getGDSIINum())
                {
                    data->conductorIn[indi].zmin = data->stackBegCoor[indj];
                    data->conductorIn[indi].zmax = data->stackEndCoor[indj];
                    data->stackCdtMark[indj] = 1;
                }
            }
        }

        // Use port information in parasitics data member to set fields
        data->numPorts = (this->para).getNPort();
        data->portCoor.reserve(data->numPorts);
        for (size_t indi = 0; indi < data->numPorts; indi++)
        {
            Port thisPort = (this->para).getPort(indi); // Get copy of port information for this interation
            int mult = thisPort.getMultiplicity(); // Multiplicity of the port
            data->portCoor.emplace_back(fdtdPort()); // Create an empty port in fdtdMesh

            // Save coordinates of the port (portCoor must have x1 < x2, y1 < y2, and z1 < z2)
            for (size_t indMult = 0; indMult < mult; indMult++)
            {
                data->portCoor[indi].x1.push_back(min(thisPort.getCoord()[6 * indMult + 0], thisPort.getCoord()[6 * indMult + 3]));
                data->portCoor[indi].y1.push_back(min(thisPort.getCoord()[6 * indMult + 1], thisPort.getCoord()[6 * indMult + 4]));
                data->portCoor[indi].z1.push_back(min(thisPort.getCoord()[6 * indMult + 2], thisPort.getCoord()[6 * indMult + 5]));
                data->portCoor[indi].x2.push_back(max(thisPort.getCoord()[6 * indMult + 0], thisPort.getCoord()[6 * indMult + 3]));
                data->portCoor[indi].y2.push_back(max(thisPort.getCoord()[6 * indMult + 1], thisPort.getCoord()[6 * indMult + 4]));
                data->portCoor[indi].z2.push_back(max(thisPort.getCoord()[6 * indMult + 2], thisPort.getCoord()[6 * indMult + 5]));
            }

            // Save multiplicity and direction of the port (portCoor uses +1 to denote in direction of increasing coordinates)
            data->portCoor[indi].multiplicity = mult;
            data->portCoor[indi].portDirection = thisPort.positiveCoordFlow();

            // Add coordinates of port to unordered maps if need be
            if (thisPort.getCoord()[0] == thisPort.getCoord()[3])
            {
                // Record this x-coordinate along the x-axis
                portCoorx->insert(thisPort.getCoord()[0]);
            }
            if (thisPort.getCoord()[1] == thisPort.getCoord()[4])
            {
                // Record this y-coordinate along the y-axis
                portCoory->insert(thisPort.getCoord()[1]);
            }
        }
    }

    // Print the solver database
    void print(vector<size_t> indLayerPrint)
    {
        // Print
        int numLayer = this->getNumLayer();
        cout << "Solver Database of IC Design, " << this->designName << ":" << endl;
        cout << " Settings for the simulation:" << endl;
        (this->settings).print(); // Print the simulation settings
        cout << " Details of " << indLayerPrint.size() << " of the " << numLayer << " layers:" << endl;
        cout << "  ------" << endl;
        for (size_t indi = 0; indi < indLayerPrint.size(); indi++)
        {
            (this->layers)[indLayerPrint[indi]].print(); // Print layer details by index number, not GDSII number
        }
        cout << " Waveforms:" << endl;
        (this->wf).print(); // Print the waveforms
        cout << " Parasitics in file " << this->outXyce << ":" << endl;
        (this->para).print(); // Print the parasitics
        cout << "------" << endl;
    }

    // Dump the solver database to SPEF file
    bool dumpSPEF()
    {
        // Attempt to open output file
        ofstream spefFile((this->outSPEF).c_str());

        // Dump
        if (spefFile.is_open())
        {
            // Write to file
            spef::Spef designPara = (this->para).toSPEF(this->designName, WRITE_THRESH);
            designPara.dump(spefFile);

            // Close file
            spefFile.close();
            return true;
        }
        else
        {
            // File could not be opened
            return false;
        }
    }

    // Dump the solver database to Xyce (SPICE) subcircuit file
    bool dumpXyce()
    {
        // Attempt to dump to Xyce File
        bool couldDump = (this->para).toXyce(this->outXyce, this->designName, WRITE_THRESH);
        return couldDump;
    }

    // Dump the solver database to CITIfile
    bool dumpCITI()
    {
        // Attempt to open output file
        ofstream citiFile((this->outCITI).c_str());

        // Dump
        if (citiFile.is_open())
        {
            // Initialize time variables
            char timeStr[80];                                                             // Character array to hold formatted time
            time_t rawtime;                                                               // Timer variable
            strftime(timeStr, sizeof(timeStr), "%Y %m %d %H %M %S", localtime(&rawtime)); // Use local timezone to format string according to CITIfile requirements
            std::string time(timeStr);                                                    // Formatted time to string parametrized constructor

            // Convert design name to uppercase
            char design[this->designName.size() + 1];
            for (size_t indi = 0; indi <= this->designName.size(); indi++)
            {
                design[indi] = toupper((char)(this->designName).c_str()[indi]);
            }

            // Retrieve frequency sweep information
            vector<double> freqs = this->para.getFreqs(); //this->settings.getFreqsHertz();
            size_t nfreq = freqs.size(); // this->settings.getNFreq();

            // Retrieve network parameter information
            char paramType = this->para.getParamType();
            size_t nPorts = this->para.getNPort();
            vector<cdMat> matParam = this->para.getParamMatrix();

            // Write a Common Instrumentation Transfer and Interchange file (CITIfile) package header
            citiFile << "CITIFILE A.01.01" << endl; // File identification and format revision code
            citiFile << "CONSTANT TIME " << time << endl; // Time stamp in correct format
            citiFile << "NAME " << design << endl; // CITIfile package name is design name all in uppercase with no spaces
            citiFile << "VAR FREQ MAG " << nfreq << endl; // Independent variable is frequency in "magnitude" format with nfreq points
            for (size_t indi = 0; indi < nPorts; indi++)
            {
                for (size_t indj = 0; indj < nPorts; indj++)
                {
                    citiFile << "DATA " << paramType << "[" << indi + 1 << "," << indj + 1 << "] RI" << endl; // Data array line for network parameter in real/imaginary format
                }
            }

            // Write the frequencies as the independent variables in the package
            citiFile << "VAR_LIST_BEGIN" << endl;
            for (size_t indFreq = 0; indFreq < nfreq; indFreq++)
            {
                citiFile << std::left << std::setw(23) << std::setprecision(17) << freqs[indFreq] << endl;
            }
            citiFile << "VAR_LIST_END" << endl;

            // Write the network parameters
            for (size_t indi = 0; indi < nPorts; indi++) // Network parameter row
            {
                for (size_t indj = 0; indj < nPorts; indj++) // Network parameter column
                {
                    citiFile << "BEGIN" << endl;
                    for (size_t indFreq = 0; indFreq < nfreq; indFreq++)
                    {
                        complex<double> pij = matParam[indFreq].coeffRef(indi, indj); // Expensive binary search to get specific index if sparse parameter matrix
                        citiFile << std::right << std::setw(23) << std::setprecision(17) << pij.real() << "," << std::left << std::setw(23) << std::setprecision(17) << pij.imag() << endl; // One entry of the parameter matrix at a given frequency (real, imaginary)
                    }
                    citiFile << "END" << endl;
                }
            }

            // Close file
            citiFile.close();
            return true;
        }
        else
        {
            // File could not be opened
            return false;
        }
    }

    // Dump the solver database to Touchstone file
    bool dumpTouchstone()
    {
        // Attempt to open output file
        ofstream tstoneFile((this->outTstone).c_str());

        // Dump
        if (tstoneFile.is_open())
        {
            // Initialize time variables
            char timeStr[80];                                                             // Character array to hold formatted time
            time_t rawtime;                                                               // Timer variable
            strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", localtime(&rawtime)); // Use local timezone to format string for a comment
            std::string time(timeStr);

            // Retrieve frequency sweep information
            vector<double> freqs = this->para.getFreqs(); //this->settings.getFreqsHertz();
            size_t nfreq = freqs.size(); // this->settings.getNFreq();

            // Retrieve network parameter information
            char paramType = this->para.getParamType();
            size_t nPorts = this->para.getNPort();
            double Zport = this->para.getPort(0).getZSource(); // Reference impedance of the first port (ohm)
            vector<cdMat> matParam = this->para.getParamMatrix();

            // Write a Touchstone file header
            tstoneFile << "[Version] 2.0" << endl; // Touchstone version 2.0 released by IBIS on April 24, 2009
            tstoneFile << "# Hz " << paramType << " RI R " << Zport << endl; // Option line with frequency units (Hz, kHz, MHz, or GHz), network parameters types (S, Y, Z, H, or G), complex number format (DB, MA, or RI), and reference resistance
            tstoneFile << "[Number of Ports] " << nPorts << endl; // Required number of ports line
            if (nPorts == 2)
            {
                tstoneFile << "[Two-Port Data Order] 12_21" << endl; // Required line for 2-ports indicating no special column order for the 2-port case
            }
            tstoneFile << "[Number of Frequencies] " << nfreq << endl; // Number of freqencies in file
            tstoneFile << "[Reference]" << endl; // Optional reference resistance on its own line (only used for S-parameters, others non-normalized)
            for (size_t indPort = 0; indPort < nPorts; indPort++)
            {
                tstoneFile << this->para.getPort(indPort).getZSource() << " "; // Each port's real impedance in order, space-separated (ohm)
            }
            tstoneFile << endl << "[Matrix Format] Full" << endl; // Close previous line and give optional matrix format line (Full, Lower, or Upper)
            tstoneFile << "! Date/Time " << time << endl; // Time stamp
            tstoneFile << "! Name " << this->designName << endl; // Design name
            tstoneFile << "! Provider: DARPA ERI IDEA/POSH Contributors" << endl;
            tstoneFile << "! Program: Xyce Writer from DARPA ERI" << endl;
            tstoneFile << "! Author: Purdue University" << endl;
            tstoneFile << "[Network Data] " << endl; // Required network data line

            // Write the network data
            tstoneFile << "!freq         "; // Begin helpful comment line
            for (size_t indi = 0; indi < nPorts; indi++)
            {
                for (size_t indj = 0; indj < nPorts; indj++)
                {
                    if ((nPorts > 2) && (indi > 0) && (indj == 0))
                    {
                        tstoneFile << "!Re" << paramType << indi + 1 << "," << indj + 1 << std::string(7 - indi / 10 - indj / 10, ' '); // Identify number and pad with spaces from string fill constructor with integer division on new row
                    }
                    else
                    {
                        tstoneFile << "Re" << paramType << indi + 1 << "," << indj + 1 << std::string(8 - indi / 10 - indj / 10, ' '); // Identify number and pad with spaces from string fill constructor with integer division
                    }
                    tstoneFile << "Im" << paramType << indi + 1 << "," << indj + 1 << std::string(8 - indi / 10 - indj / 10, ' ');
                    if (((indj + 1) % 4 == 0) && (indj + 1 != nPorts) && (nPorts > 2))
                    {
                        tstoneFile << endl << "!" << std::string(23, ' '); // Put no more than 4 columns of parameters on one file line
                    }
                }
                if (nPorts > 2)
                {
                    tstoneFile << endl; // End the row of the long, helpful comment
                }
            }
            tstoneFile << endl; // End helpful comment line
            for (size_t indFreq = 0; indFreq < nfreq; indFreq++)
            {
                tstoneFile << std::left << std::setw(23) << std::setprecision(17) << freqs[indFreq] << " ";
                for (size_t indi = 0; indi < nPorts; indi++)
                {
                    for (size_t indj = 0; indj < nPorts; indj++)
                    {
                        complex<double> pij = matParam[indFreq].coeffRef(indi, indj); // Expensive binary search to get specific index if sparse parameter matrix
                        tstoneFile << std::left << std::setw(23) << std::setprecision(17) << pij.real() << " "; // Real part of parameter entry at this frequency
                        tstoneFile << std::left << std::setw(23) << std::setprecision(17) << pij.imag() << " "; // Imaginary part of parameter entry at this frequency
                        if (((indj + 1) % 4 == 0) && (indj + 1 != nPorts) && (nPorts > 2))
                        {
                            tstoneFile << endl << std::string(24, ' '); // Put no more than 4 columns of parameters on one file line
                        }
                    }
                    if (nPorts > 2)
                    {
                        tstoneFile << " ! row " << indi + 1 << endl; // Comment to mark the end of a row
                    }
                }
                if (nPorts <= 2)
                {
                    tstoneFile << endl; // All rows of matrix on a single line for 1-port and 2-ports
                }
            }
            tstoneFile << "[End]" << endl;

            // Close file
            tstoneFile.close();
            return true;
        }
        else
        {
            // File could not be opened
            return false;
        }
    }

    // Destructor
    ~SolverDataBase()
    {
        // Nothing
    }
};

#endif
