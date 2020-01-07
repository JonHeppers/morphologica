#include "Visual.h"

#include "GL3/gl3.h"
#include "GL/glext.h"

#include <iostream>
using std::cout;
using std::cerr;
using std::endl;

#include <cstring>
using std::strlen;

#include "Quaternion.h"
using morph::Quaternion;

using morph::ShaderInfo;

morph::Visual::Visual(int width, int height, const string& title)
    : window_w(width)
    , window_h(height)
{
    if (!glfwInit()) {
        // Initialization failed
        cerr << "GLFW initialization failed!" << endl;
    }

    // Set up error callback
    glfwSetErrorCallback (morph::Visual::errorCallback);

    // See https://www.glfw.org/docs/latest/monitor_guide.html
    GLFWmonitor* primary = glfwGetPrimaryMonitor();
    float xscale, yscale;
    glfwGetMonitorContentScale(primary, &xscale, &yscale);
    cout << "Monitor xscale: " << xscale << ", monitor yscale: " << yscale << endl;

    glfwWindowHint (GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint (GLFW_CONTEXT_VERSION_MINOR, 5);
    this->window = glfwCreateWindow (width, height, title.c_str(), NULL, NULL);
    if (!this->window) {
        // Window or OpenGL context creation failed
        cerr << "GLFW window creation failed!" << endl;
    }

    // Fix the event handling for benefit of static functions.
    this->setEventHandling();

    // Set up callbacks
    glfwSetKeyCallback (this->window,         VisualBase::key_callback_dispatch);
    glfwSetMouseButtonCallback (this->window, VisualBase::mouse_button_callback_dispatch);
    glfwSetCursorPosCallback (this->window,   VisualBase::cursor_position_callback_dispatch);
    glfwSetWindowSizeCallback (this->window,  VisualBase::window_size_callback_dispatch);
    glfwSetScrollCallback (this->window,  VisualBase::scroll_callback_dispatch);

    glfwMakeContextCurrent (this->window);

    // Load up the shaders
    ShaderInfo shaders[] = {
        {GL_VERTEX_SHADER, "Visual.vert.glsl" },
        {GL_FRAGMENT_SHADER, "Visual.frag.glsl" },
        {GL_NONE, NULL }
    };

    this->shaderprog = this->LoadShaders (shaders);

    // shaderprog is bound here, and never unbound
    glUseProgram (this->shaderprog);

    // Now client code can set up HexGridVisuals.
    glEnable (GL_DEPTH_TEST);
    //glEnable(GL_CULL_FACE);
    //glDisable(GL_DEPTH_TEST);
    //glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

morph::Visual::~Visual()
{
    glfwDestroyWindow (this->window);
    glfwTerminate();
}

void
morph::Visual::setPerspective (void)
{
    // Calculate aspect ratio
    float aspect = float(this->window_w) / float(this->window_h ? this->window_h : 1);
    //cout << "aspect = " << aspect << endl;
    // Set near plane to 3.0, far plane to 7.0, field of view 45 degrees
    const float zNear = 1.0, zFar = 8.0, fov = 90.0;
    // Reset projection
    this->projection.setToIdentity();
    // Set perspective projection
    this->projection.perspective (fov, aspect, zNear, zFar);
}

void
morph::Visual::render (void)
{
    // Can avoid this by getting window size into members only when window size changes.
    const double retinaScale = 1; // devicePixelRatio()?

    glViewport (0, 0, this->window_w * retinaScale, this->window_h * retinaScale);

    // Set the perspective from the width/height
    this->setPerspective();

    // Calculate model view transformation - transforming from "model space" to "worldspace".
    this->rotmat.setToIdentity();
    this->rotmat.translate (this->scenetrans); // send backwards into distance
    this->rotmat.rotate (this->rotation);
    //cout << "Rotation quaternion: ";
    //this->rotation.output();
    //cout << "rotmat:" << endl;
    //this->rotmat.output();

    // Set modelview-projection matrix
    this->viewproj = this->projection * this->rotmat;
    //cout << "Query shaderprog "  << this->shaderprog << " for mvp_matrix location" << endl;
    GLint loc = glGetUniformLocation (this->shaderprog, (const GLchar*)"mvp_matrix");
    if (loc == -1) {
        cout << "No mvp_matrix? loc: " << loc << endl;
    } else {
        // Set the uniform:
        glUniformMatrix4fv (loc, 1, GL_FALSE, this->viewproj.mat.data());
    }

    // Clear color buffer and **also depth buffer**
    glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Can set a background colour:
    //static const float white[] = { 1.0f, 1.0f, 1.0f, 0.5f };
    //glClearBufferfv (GL_COLOR, 0, white); // This line works...

    // Render it.
    vector<HexGridVisual*>::iterator hgvi = this->hexGridVis.begin();
    while (hgvi != this->hexGridVis.end()) {
        (*hgvi)->render();
        ++hgvi;
    }

    glfwSwapBuffers (this->window);
}

void
morph::Visual::updateHexGridVisual (const unsigned int gridId,
                                    const vector<float>& data)
{
    // Replace grids[gridId].data
}

unsigned int
morph::Visual::addHexGridVisual (const HexGrid* hg,
                                 const vector<float>& data,
                                 const array<float, 3> offset)
{
    // Copy x/y positions from the HexGrid and make a copy of the data as vertices.
    HexGridVisual* hgv1 = new HexGridVisual(this->shaderprog, hg, &data, offset);
    this->hexGridVis.push_back (hgv1);

    return 0;
}

unsigned int
morph::Visual::addTriangleVisual (void)
{
    // Copy x/y positions from the HexGrid and make a copy of the data as vertices.
    TriangleVisual* tv1 = new TriangleVisual(this->shaderprog);
    this->triangleVis.push_back (tv1);

    return 0;
}

const GLchar*
morph::Visual::ReadShader (const char* filename)
{
    FILE* infile = fopen (filename, "rb");

    if (!infile) {
        cerr << "Unable to open file '" << filename << "'" << std::endl;
        return NULL;
    }

    fseek (infile, 0, SEEK_END);
    int len = ftell (infile);
    fseek (infile, 0, SEEK_SET);

    GLchar* source = new GLchar[len+1];

    fread (source, 1, len, infile);
    fclose (infile);

    source[len] = 0;

    return const_cast<const GLchar*>(source);
}

GLuint
morph::Visual::LoadShaders (ShaderInfo* shaders)
{
    if (shaders == NULL) { return 0; }

    GLuint program = glCreateProgram();

    GLboolean shaderCompilerPresent = GL_FALSE;
    glGetBooleanv (GL_SHADER_COMPILER, &shaderCompilerPresent);
    if (shaderCompilerPresent == GL_FALSE) {
        cerr << "Shader compiler NOT present!" << endl;
    } else {
        cout << "Shader compiler present" << endl;
    }

    ShaderInfo* entry = shaders;
    while (entry->type != GL_NONE) {
        GLuint shader = glCreateShader (entry->type);
        entry->shader = shader;

        const GLchar* source = morph::Visual::ReadShader (entry->filename);
        if (source == NULL) {
            for (entry = shaders; entry->type != GL_NONE; ++entry) {
                glDeleteShader (entry->shader);
                entry->shader = 0;
            }
            return 0;
#ifdef DEBUG
        } else {
            cout << "Compiling this shader: " << endl << "-----" << endl;
            cout << source << "-----" << endl;
#endif
        }
        GLint slen = (GLint)strlen (source);
        glShaderSource (shader, 1, &source, &slen);
        delete [] source;

        glCompileShader (shader);
        GLenum shaderError = glGetError();
        if (shaderError == GL_INVALID_VALUE) {
            cout << "Shader compilation resulted in GL_INVALID_VALUE" << endl;
        } else if (shaderError == GL_INVALID_OPERATION) {
            cout << "Shader compilation resulted in GL_INVALID_OPERATION" << endl;
        } // shaderError is 0

        GLint compiled = GL_FALSE;
        glGetShaderiv (shader, GL_COMPILE_STATUS, &compiled);
        if (compiled != GL_TRUE) {
            GLsizei len = 0;
            glGetShaderiv (shader, GL_INFO_LOG_LENGTH, &len);
            cout << "compiled is GL_FALSE. log length is " << len
                 << " compiled has value " << compiled <<  endl;
            if (len > 0) {
                GLchar* log = new GLchar[len+1];
                glGetShaderInfoLog (shader, len, &len, log);
                std::cerr << "Shader compilation failed: " << (char*)log << std::endl;
                delete [] log;
            }
            return 0;
#ifdef DEBUG
        } else {
            cout << "shader compiled" << endl;
#endif
        }

        glAttachShader (program, shader);

        ++entry;
    }

    glLinkProgram (program);

    GLint linked;
    glGetProgramiv (program, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLsizei len;
        glGetProgramiv( program, GL_INFO_LOG_LENGTH, &len );
        GLchar* log = new GLchar[len+1];
        glGetProgramInfoLog( program, len, &len, log );
        std::cerr << "Shader linking failed: " << log << std::endl;
        delete [] log;

        for (entry = shaders; entry->type != GL_NONE; ++entry) {
            glDeleteShader (entry->shader);
            entry->shader = 0;
        }

        return 0;
    } else {
        cout << "Good, shader is linked." << endl;
    }

    return program;
}

/*!
 * GLFW callback functions
 */
//@{

void
morph::Visual::errorCallback (int error, const char* description)
{
    cerr << "Error: " << description << " (code "  << error << ")" << endl;
}

void
morph::Visual::key_callback (GLFWwindow* window, int key, int scancode, int action, int mods)
{
    // Exit action
    if (key == GLFW_KEY_X && action == GLFW_PRESS) {
        cout << "User requested exit." << endl;
        this->readyToFinish = true;
    }

    // Reset view to default
    if (key == GLFW_KEY_A && action == GLFW_PRESS) {
        cout << "Reset to default view" << endl;
        // Reset translation
        this->scenetrans = this->scenetrans_default;
        // Reset rotation
        Quaternion<float> rt;
        this->rotation = rt;

        this->render();
    }
}

void
morph::Visual::mouse_button_callback (GLFWwindow* window, int button, int action, int mods)
{
    // button is the button number, action is either key press (1) or key release (0)
    cout << "button: " << button << " action: " << (action==1?("press"):("release")) << endl;

    // Record the position at which the button was pressed
    if (action == 1) { // Button down
        this->mousePressPosition = this->cursorpos;
    }

    if (action == 0 && button == 1) {
        // This is release of the translation button

        // The difference between the cursor when the mouse was pressed, and now.
        Vector2<float> diff = this->cursorpos;
        diff -= this->mousePressPosition;
        array<float, 4> trans = { 2.0*diff.x/static_cast<float>(this->window_w), // -1 to 1 is 2
                                  2.0*diff.y/static_cast<float>(this->window_h),
                                  this->scenetrans.z,//1.0, // zNear
                                  0.0 }; // or 1.0?
        // Compute the inverse projection
        TransformMatrix<float> invproj = this->viewproj.invert(); // not view proj?
        cout << "------------------------"<<endl;
        //cout << "Projection matrix: " << endl;
        //this->projection.output();
        //cout << "View-projection matrix:" << endl;
        //this->viewproj.output();
        //cout << "View-projection inverse:" << endl;
        //invproj.output();
        array<float, 4> v;
        v = invproj * trans;
        cout << "trans:            ("
             << trans[0]<<","<<trans[1]<<","<<trans[2]<<","<<trans[3]<<")" <<endl;
        //cout << "abs cursor x, y:  ("
        //     << x <<","<< y <<") with window width " << this->window_w <<endl;

        cout << "invproj * trans:  ("<< v[0]<<","<< v[1]<<","<< v[2]<<","<< v[3]<<")"<<endl;

        this->scenetrans.x += v[0];
        this->scenetrans.y -= v[1];
        cout << "scenetrans:        ("
             << this->scenetrans.x << ","
             << this->scenetrans.y << ","
             << this->scenetrans.z << ")" << endl;

        this->render(); // updates viewproj
    }

    if (button == 0) { // Primary button means rotate
        this->rotateMode = (action == 1);
    } else if (button == 1) { // Secondary button means translate
        this->translateMode = (action == 1);
    }
}

void
morph::Visual::cursor_position_callback (GLFWwindow* window, double x, double y)
{
    this->cursorpos.x = static_cast<float>(x);
    this->cursorpos.y = static_cast<float>(y);

#if 0
    // The difference between the cursor when the mouse was pressed, and now.
    Vector2<float> diff(static_cast<float>(x), static_cast<float>(y));
    diff -= this->mousePressPosition;

    // Now use mousePressPosition as a record of the last cursor position.
    this->mousePressPosition = this->cursorpos;

    if (this->rotateMode) {
        //cout << "diff: " << diff.x << "," << diff.y << endl;
        // Rotation axis is perpendicular to the mouse position difference vector
        Vector3<float> n(diff.y, diff.x, 0.0f);
        n.renormalize();
        // Accelerate angular speed relative to the length of the mouse sweep
        float rotamount = diff.length() / 100.0;
        // Calculate new rotation axis as weighted sum
        this->rotationAxis = this->rotationAxis + (n * rotamount);
        this->rotationAxis.renormalize();
        // Update rotation
        Quaternion<float> rotationQuaternion;
        rotationQuaternion.initFromAxisAngle (this->rotationAxis, rotamount);
        this->rotation.premultiply (rotationQuaternion);
    }

    if (this->translateMode) {
        // Convert cursor pos in screen coords into object:
        // Multiply (ScreenX,ScreenY,Near,1) by the full transformation to get the right vector in world space.

        // Need to apply the inverse transforms here to automatically compute
        // scenetrans_mousestepsize. There's a factor here which is the units in the
        // 3D space which w x h pixels span.
        array<float, 4> trans = { 2.0*diff.x/static_cast<float>(this->window_w), // -1 to 1 is 2
                                  2.0*diff.y/static_cast<float>(this->window_h),
                                  this->scenetrans.z,//1.0, // zNear
                                  0.0 }; // or 1.0?
        // Compute the inverse projection
        TransformMatrix<float> invproj = this->viewproj.invert(); // not view proj?
        cout << "------------------------"<<endl;
        //cout << "Projection matrix: " << endl;
        //this->projection.output();
        //cout << "View-projection matrix:" << endl;
        //this->viewproj.output();
        //cout << "View-projection inverse:" << endl;
        //invproj.output();
        array<float, 4> v;
        v = invproj * trans;
        cout << "trans:            ("
             << trans[0]<<","<<trans[1]<<","<<trans[2]<<","<<trans[3]<<")" <<endl;
        cout << "abs cursor x, y:  ("
             << x <<","<< y <<") with window width " << this->window_w <<endl;

        cout << "invproj * trans:  ("<< v[0]<<","<< v[1]<<","<< v[2]<<","<< v[3]<<")"<<endl;

        this->scenetrans.x += v[0];
        this->scenetrans.y -= v[1];
        cout << "scenetrans:        ("
             << this->scenetrans.x << ","
             << this->scenetrans.y << ","
             << this->scenetrans.z << ")" << endl;
    }

    this->render(); // updates viewproj
#endif
}

void
morph::Visual::window_size_callback (GLFWwindow* window, int width, int height)
{
    this->window_w = width;
    this->window_h = height;
    this->render();
}

void
morph::Visual::scroll_callback (GLFWwindow* window, double xoffset, double yoffset)
{
    // x and y can be +/- 1
    this->scenetrans.x -= xoffset * this->scenetrans_stepsize;
    if (this->translateMode) {
        this->scenetrans.y/*z really*/ += yoffset * this->scenetrans_stepsize;
        cout << "scenetrans.y = " << this->scenetrans.y << endl;
    } else {
        this->scenetrans.z += yoffset * this->scenetrans_stepsize;
    }
    this->render();
}
//@}
