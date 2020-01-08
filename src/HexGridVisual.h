#ifndef _HEXGRIDVISUAL_H_
#define _HEXGRIDVISUAL_H_

#include "GL3/gl3.h"
#include "GL/glext.h"

#include "tools.h"

#include "HexGrid.h"

#include <iostream>
using std::cout;
using std::endl;

#include <vector>
using std::vector;
#include <array>
using std::array;

typedef GLuint VBOint;
#define VBO_ENUM_TYPE GL_UNSIGNED_INT

/*!
 * Macros for testing neighbours. The step along for neighbours on the
 * rows above/below is given by:
 *
 * Dest  | step
 * ----------------------
 * NNE   | +rowlen
 * NNW   | +rowlen - 1
 * NSW   | -rowlen
 * NSE   | -rowlen + 1
 */
//@{
#define NE(hi) (this->hg->d_ne[hi])
#define HAS_NE(hi) (this->hg->d_ne[hi] == -1 ? false : true)

#define NW(hi) (this->hg->d_nw[hi])
#define HAS_NW(hi) (this->hg->d_nw[hi] == -1 ? false : true)

#define NNE(hi) (this->hg->d_nne[hi])
#define HAS_NNE(hi) (this->hg->d_nne[hi] == -1 ? false : true)

#define NNW(hi) (this->hg->d_nnw[hi])
#define HAS_NNW(hi) (this->hg->d_nnw[hi] == -1 ? false : true)

#define NSE(hi) (this->hg->d_nse[hi])
#define HAS_NSE(hi) (this->hg->d_nse[hi] == -1 ? false : true)

#define NSW(hi) (this->hg->d_nsw[hi])
#define HAS_NSW(hi) (this->hg->d_nsw[hi] == -1 ? false : true)
//@}

#define IF_HAS_NE(hi, yesval, noval)  (HAS_NE(hi)  ? yesval : noval)
#define IF_HAS_NNE(hi, yesval, noval) (HAS_NNE(hi) ? yesval : noval)
#define IF_HAS_NNW(hi, yesval, noval) (HAS_NNW(hi) ? yesval : noval)
#define IF_HAS_NW(hi, yesval, noval)  (HAS_NW(hi)  ? yesval : noval)
#define IF_HAS_NSW(hi, yesval, noval) (HAS_NSW(hi) ? yesval : noval)
#define IF_HAS_NSE(hi, yesval, noval) (HAS_NSE(hi) ? yesval : noval)

namespace morph {

    //! Forward declaration of a templated Visual class
    class Visual;

    //! The locations for the position, normal and colour vertex attributes in the GLSL program
    enum AttribLocn { posnLoc = 0, normLoc = 1, colLoc = 2 };

    //! The template arguemnt Flt is the type of the data which this HexGridVisual will visualize.
    template <class Flt>
    class HexGridVisual
    {
    public:
        HexGridVisual(GLuint sp,
                      const HexGrid* _hg,
                      const vector<Flt>* _data,
                      const array<float, 3> _offset) {
            // Set up...
            this->shaderprog = sp;
            this->offset = _offset;
            this->hg = _hg;
            this->data = _data;

            // Do the computations to initialize the vertices that well
            // represent the HexGrid.
            this->initializeVerticesHexesInterpolated();
            //this->initializeVerticesTris();

            // Create vertex array object
            glCreateVertexArrays (1, &this->vao);
            glBindVertexArray (this->vao);

            // Allocate/create the vertex buffer objects
            this->vbos = new GLuint[numVBO];
            glCreateBuffers (numVBO, this->vbos);

            // Set up the indices buffer
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->vbos[idxVBO]);
            int sz = this->indices.size() * sizeof(VBOint);
            cout << "indices sz = " << sz << " bytes" << endl;
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, sz, this->indices.data(), GL_STATIC_DRAW);

            // Binds data from the "C++ world" to the OpenGL shader world for
            // "position", "normalin" and "color"
            this->setupVBO (this->vbos[posnVBO], this->vertexPositions, posnLoc);
            this->setupVBO (this->vbos[normVBO], this->vertexNormals, normLoc);
            this->setupVBO (this->vbos[colVBO], this->vertexColors, colLoc);

            // Possibly release (unbind) the vertex buffers (but not index buffer)
            // Possible glVertexAttribPointer and glEnableVertexAttribArray?
            glUseProgram (shaderprog);

            // Here's how to unbind the VAO
            //glBindVertexArray(0);
        }

        ~HexGridVisual() {
            // destroy buffers
            glDeleteBuffers (4, vbos);
            delete (this->vbos);
        }

        //! Initialize vertex buffer objects and vertex array object.
        //@{
        //! Initialize as triangled. Gives a smooth surface with much
        //! less comput than initializeVerticesHexesInterpolated.
        void initializeVerticesTris (void) {
            unsigned int nhex = this->hg->num();
            for (unsigned int hi = 0; hi < nhex; ++hi) {
                this->vertex_push (this->hg->d_x[hi], this->hg->d_y[hi], (*this->data)[hi], this->vertexPositions);
                this->vertex_push (morph::Tools::getJetColorF((double)(*this->data)[hi]+0.5), this->vertexColors);
                this->vertex_push (0.0f, 0.0f, 1.0f, this->vertexNormals);
            }

            // Build indices based on neighbour relations in the HexGrid
            for (unsigned int hi = 0; hi < nhex; ++hi) {
                if (HAS_NNE(hi) && HAS_NE(hi)) {
                    cout << "1st triangle " << hi << "->" << NNE(hi) << "->" << NE(hi) << endl;
                    this->indices.push_back (hi);
                    this->indices.push_back (NNE(hi));
                    this->indices.push_back (NE(hi));
                }

                if (HAS_NW(hi) && HAS_NSW(hi)) {
                    cout << "2nd triangle " << hi << "->" << NW(hi) << "->" << NSW(hi) << endl;
                    this->indices.push_back (hi);
                    this->indices.push_back (NW(hi));
                    this->indices.push_back (NSW(hi));
                }
            }
        }

        //! Initialize as hexes, with z position of each of the 6
        //! outer edges of the hexes interpolated, but a single colour
        //! for each hex. Gives a smooth surface.
        void initializeVerticesHexesInterpolated (void) {
            float sr = this->hg->getSR();
            float vne = this->hg->getVtoNE();
            float lr = this->hg->getLR();

            unsigned int nhex = this->hg->num();
            unsigned int idx = 0;

            float datum = 0.0f;
            float third = 0.33333333333333f;
            float half = 0.5f;
            for (unsigned int hi = 0; hi < nhex; ++hi) {

                // Use a single colour for each hex, even though hex z positions are interpolated
                array<float, 3> clr = morph::Tools::getJetColorF((double)(*this->data)[hi]+0.5);

                // First push the 7 positions of the triangle vertices, starting with the centre
                this->vertex_push (this->hg->d_x[hi], this->hg->d_y[hi], (*this->data)[hi], this->vertexPositions);

                if (HAS_NNE(hi) && HAS_NE(hi)) {
                    // Compute mean of this->data[hi] and NE and E hexes
                    datum = third * ((*this->data)[hi] + (*this->data)[NNE(hi)] + (*this->data)[NE(hi)]);
                } else if (HAS_NNE(hi) || HAS_NE(hi)) {
                    if (HAS_NNE(hi)) {
                        datum = half * ((*this->data)[hi] + (*this->data)[NNE(hi)]);
                    } else {
                        datum = half * ((*this->data)[hi] + (*this->data)[NE(hi)]);
                    }
                } else {
                    datum = (*this->data)[hi];
                }
                this->vertex_push (this->hg->d_x[hi]+sr, this->hg->d_y[hi]+vne, datum, this->vertexPositions);

                // SE
                if (HAS_NE(hi) && HAS_NSE(hi)) {
                    datum = third * ((*this->data)[hi] + (*this->data)[NE(hi)] + (*this->data)[NSE(hi)]);
                } else if (HAS_NE(hi) || HAS_NSE(hi)) {
                    if (HAS_NE(hi)) {
                        datum = half * ((*this->data)[hi] + (*this->data)[NE(hi)]);
                    } else {
                        datum = half * ((*this->data)[hi] + (*this->data)[NSE(hi)]);
                    }
                } else {
                    datum = (*this->data)[hi];
                }
                this->vertex_push (this->hg->d_x[hi]+sr, this->hg->d_y[hi]-vne, datum, this->vertexPositions);

                // S
                if (HAS_NSE(hi) && HAS_NSW(hi)) {
                    datum = third * ((*this->data)[hi] + (*this->data)[NSE(hi)] + (*this->data)[NSW(hi)]);
                } else if (HAS_NSE(hi) || HAS_NSW(hi)) {
                    if (HAS_NSE(hi)) {
                        datum = half * ((*this->data)[hi] + (*this->data)[NSE(hi)]);
                    } else {
                        datum = half * ((*this->data)[hi] + (*this->data)[NSW(hi)]);
                    }
                } else {
                    datum = (*this->data)[hi];
                }
                this->vertex_push (this->hg->d_x[hi], this->hg->d_y[hi]-lr, datum, this->vertexPositions);

                // SW
                if (HAS_NW(hi) && HAS_NSW(hi)) {
                    datum = third * ((*this->data)[hi] + (*this->data)[NW(hi)] + (*this->data)[NSW(hi)]);
                } else if (HAS_NW(hi) || HAS_NSW(hi)) {
                    if (HAS_NW(hi)) {
                        datum = half * ((*this->data)[hi] + (*this->data)[NW(hi)]);
                    } else {
                        datum = half * ((*this->data)[hi] + (*this->data)[NSW(hi)]);
                    }
                } else {
                    datum = (*this->data)[hi];
                }
                this->vertex_push (this->hg->d_x[hi]-sr, this->hg->d_y[hi]-vne, datum, this->vertexPositions);

                // NW
                if (HAS_NNW(hi) && HAS_NW(hi)) {
                    datum = third * ((*this->data)[hi] + (*this->data)[NNW(hi)] + (*this->data)[NW(hi)]);
                } else if (HAS_NNW(hi) || HAS_NW(hi)) {
                    if (HAS_NNW(hi)) {
                        datum = half * ((*this->data)[hi] + (*this->data)[NNW(hi)]);
                    } else {
                        datum = half * ((*this->data)[hi] + (*this->data)[NW(hi)]);
                    }
                } else {
                    datum = (*this->data)[hi];
                }
                this->vertex_push (this->hg->d_x[hi]-sr, this->hg->d_y[hi]+vne, datum, this->vertexPositions);

                // N
                if (HAS_NNW(hi) && HAS_NNE(hi)) {
                    datum = third * ((*this->data)[hi] + (*this->data)[NNW(hi)] + (*this->data)[NNE(hi)]);
                } else if (HAS_NNW(hi) || HAS_NNE(hi)) {
                    if (HAS_NNW(hi)) {
                        datum = half * ((*this->data)[hi] + (*this->data)[NNW(hi)]);
                    } else {
                        datum = half * ((*this->data)[hi] + (*this->data)[NNE(hi)]);
                    }
                } else {
                    datum = (*this->data)[hi];
                }
                this->vertex_push (this->hg->d_x[hi], this->hg->d_y[hi]+lr, datum, this->vertexPositions);

                // All normal point up
                this->vertex_push (0.0f, 0.0f, 1.0f, this->vertexNormals);
                this->vertex_push (0.0f, 0.0f, 1.0f, this->vertexNormals);
                this->vertex_push (0.0f, 0.0f, 1.0f, this->vertexNormals);
                this->vertex_push (0.0f, 0.0f, 1.0f, this->vertexNormals);
                this->vertex_push (0.0f, 0.0f, 1.0f, this->vertexNormals);
                this->vertex_push (0.0f, 0.0f, 1.0f, this->vertexNormals);
                this->vertex_push (0.0f, 0.0f, 1.0f, this->vertexNormals);

                // Seven vertices with the same colour
                this->vertex_push (clr, this->vertexColors);
                this->vertex_push (clr, this->vertexColors);
                this->vertex_push (clr, this->vertexColors);
                this->vertex_push (clr, this->vertexColors);
                this->vertex_push (clr, this->vertexColors);
                this->vertex_push (clr, this->vertexColors);
                this->vertex_push (clr, this->vertexColors);

                // Define indices now to produce the 6 triangles in the hex
                this->indices.push_back (idx+1);
                this->indices.push_back (idx);
                this->indices.push_back (idx+2);

                this->indices.push_back (idx+2);
                this->indices.push_back (idx);
                this->indices.push_back (idx+3);

                this->indices.push_back (idx+3);
                this->indices.push_back (idx);
                this->indices.push_back (idx+4);

                this->indices.push_back (idx+4);
                this->indices.push_back (idx);
                this->indices.push_back (idx+5);

                this->indices.push_back (idx+5);
                this->indices.push_back (idx);
                this->indices.push_back (idx+6);

                this->indices.push_back (idx+6);
                this->indices.push_back (idx);
                this->indices.push_back (idx+1);

                idx += 7; // 7 vertices (each of 3 floats for x/y/z), 18 indices.

#if 0
                cout << "vertexPositions size: " << vertexPositions.size() << endl;
                cout << "vertexNormals size: " << vertexNormals.size() << endl;
                cout << "vertexColors size: " << vertexPositions.size() << endl;
                cout << "indices size: " << indices.size() << " elements" << endl;
#endif
            }
            cout << "vertexPositions size: " << vertexPositions.size() << " elements" << endl;
            cout << "vertexNormals size: " << vertexNormals.size() << " elements" << endl;
            cout << "vertexColors size: " << vertexPositions.size() << " elements" << endl;
            cout << "indices size: " << indices.size() << " elements" << endl;
        }

        //! Initialize as hexes, with a step quad between each
        //! hex. Might look cool. Writeme.
        void initializeVerticesHexesStepped (void) {}
        //@}

        //! Render the HexGridVisual
        void render (void) {
            glBindVertexArray (this->vao);
            //cout << "Render " << this->indices.size() << " vertices" << endl;
            glDrawElements (GL_TRIANGLES, this->indices.size(), VBO_ENUM_TYPE, 0);
            glBindVertexArray(0);
        }

        //! The offset of this HexGridVisual
        array<float, 3> offset;

    private:
        //! This enum contains the positions within the vbo array of the different vertex buffer objects
        enum VBOPos { posnVBO, normVBO, colVBO, idxVBO, numVBO };

        //! The parent Visual object - provides access to the shader prog
        const Visual* parent;

        //! The HexGrid to visualize
        const HexGrid* hg;

        //! The data to visualize as z/colour
        const vector<Flt>* data;

        //! A copy of the reference to the shader program
        GLuint shaderprog;

        // Add a way to control the scaling scheme here.

        /*!
         * Compute positions and colours of vertices for the hexes and
         * store in these:
         */
        //@{
        //! The OpenGL Vertex Array Object
        GLuint vao;

        //! Vertex Buffer Objects stored in an array
        GLuint* vbos;

        //! CPU-side data for indices
        vector<VBOint> indices;
        //! CPU-side data for vertex positions
        vector<float> vertexPositions;
        //! CPU-side data for vertex normals
        vector<float> vertexNormals;
        //! CPU-side data for vertex colours
        vector<float> vertexColors;
        //@}

        //! I guess we'll need a shader program.
        GLuint* shaderProgram;

        //! Push three floats onto the vector of floats @vp
        //@{
        void vertex_push (const float& x, const float& y, const float& z, vector<float>& vp) {
            vp.push_back (x);
            vp.push_back (y);
            vp.push_back (z);
        }
        void vertex_push (const array<float, 3>& arr, vector<float>& vp) {
            vp.push_back (arr[0]);
            vp.push_back (arr[1]);
            vp.push_back (arr[2]);
        }
        //@}

        //! Set up a vertex buffer object
        void setupVBO (GLuint& buf, vector<float>& dat, unsigned int bufferAttribPosition) {
            int sz = dat.size() * sizeof(float);
            glBindBuffer (GL_ARRAY_BUFFER, buf);
            glBufferData (GL_ARRAY_BUFFER, sz, dat.data(), GL_STATIC_DRAW);
            glVertexAttribPointer (bufferAttribPosition, 3, GL_FLOAT, GL_FALSE, 0, (void*)(0));
            glEnableVertexAttribArray (bufferAttribPosition);
        }

    };

} // namespace morph

#endif // _HEXGRIDVISUAL_H_
