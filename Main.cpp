// ======================================================================
//  PAINT CON BRESENHAM  (OpenGL + GLFW + GLAD + Dear ImGui)
//
//  IDEA GENERAL (para la exposicion):
//  - El lienzo es una grilla de 100 x 100 "pixeles". Cada pixel es un cuadrado
//    formado por 2 triangulos (4 vertices), y "pintar un pixel" = cambiar el color de esos 4 vertices.
//  - Todo se dibuja con Bresenham, que trabaja SOLO con enteros (sumas y restas, sin decimales):
//        * Lineas   -> LineBresenhamH / LineBresenhamV (segun la pendiente)
//        * Figuras  -> rectangulo, triangulo y poligonos: se arman uniendo vertices con LINEAS de Bresenham
//        * Circulos -> PaintCircleBresenham (algoritmo del punto medio, usa simetria de 8 octantes)
//  - El mouse dibuja sobre la grilla; el panel de ImGui (a la derecha) elige herramienta y color.
//  - La ULTIMA linea o circulo dibujado se puede GIRAR 45 o 90 grados con los botones del panel:
//    se giran sus coordenadas con la matriz de rotacion y se vuelve a dibujar con Bresenham.
//  - Ctrl+Z = deshacer, ESC = cerrar el programa
// ======================================================================

#include <iostream>    // Libreria de entrada/salida: aqui solo usamos std::cout para mostrar errores en consola
#include <vector>      // std::vector: arreglo dinamico que crece solo (guarda vertices, indices, puntos...)
#include <cmath>       // Funciones matematicas: sqrt (raiz), atan2 (angulo), cos, sin, lround (redondear a entero)
#include <cstdlib>     // std::abs para numeros enteros (valor absoluto)
#include <algorithm>   // std::swap (intercambiar dos valores), std::min y std::max

#include <glad/glad.h>    // GLAD: carga las funciones de OpenGL. DEBE incluirse ANTES que GLFW
#include <GLFW/glfw3.h>   // GLFW: crea la ventana y el contexto OpenGL, y maneja mouse y teclado

#include "imgui.h"                // Dear ImGui: libreria de interfaz (botones, sliders, selector de color)
#include "imgui_impl_glfw.h"      // Conecta ImGui con GLFW (le pasa el mouse y el teclado)
#include "imgui_impl_opengl3.h"   // Conecta ImGui con OpenGL 3 (para que pueda dibujarse en pantalla)

// ---------------------------------------------------------------------
//  SHADERS: programas pequenos que se ejecutan en la GPU (se escriben como texto GLSL)
// ---------------------------------------------------------------------
const char* vertexShaderSource = "#version 330 core\n"   // Texto del vertex shader. Primera linea: version de GLSL (3.30 core)
"layout (location = 0) in vec3 aPos;\n"                  // Entrada 0 de cada vertice: posicion (x,y,z)
"layout (location = 1) in vec4 aColor;\n"                // Entrada 1 de cada vertice: color (r,g,b,a)
"out vec4 VertexColor;\n"                                // Salida: el color se envia al fragment shader
"void main()\n"                                          // Funcion principal: se ejecuta UNA vez por cada vertice
"{\n"                                                    // Abre el cuerpo de main
"   gl_Position = vec4(aPos, 1.0);\n"                    // Posicion final del vertice en pantalla (w = 1.0 porque es un punto)
"   VertexColor = aColor;\n"                             // Pasa el color del vertice al fragment shader
"}\0";                                                   // Cierra main; \0 marca el fin del texto (terminador de cadena en C)

const char* fragmentShaderSource = "#version 330 core\n" // Texto del fragment shader; misma version de GLSL
"in vec4 VertexColor;\n"                                 // Entrada: el color que viene del vertex shader
"out vec4 FragColor;\n"                                  // Salida: color final del pixel
"void main()\n"                                          // Funcion principal: se ejecuta UNA vez por cada pixel a pintar
"{\n"                                                    // Abre el cuerpo de main
"   FragColor = VertexColor;\n"                          // El pixel se pinta exactamente con el color recibido
"}\n\0";                                                 // Cierra main y termina la cadena con \0

// ---------------------------------------------------------------------
//  ESTRUCTURAS Y VARIABLES GLOBALES
// ---------------------------------------------------------------------
struct VertexSquare {      // Tipo de dato que representa UN vertice
    float pos[3];          // Posicion: x, y, z
    float color[4];        // Color: r, g, b, a (cada valor de 0.0 a 1.0). El orden pos -> color importa: OpenGL lo lee asi
};                         // Fin de la estructura

struct Punto { int x, y; };   // Representa una CELDA de la grilla: x = columna, y = fila

static GLuint VAO = 0, VBO = 0, EBO = 0;   // IDs de objetos OpenGL: VAO = configuracion de atributos, VBO = vertices, EBO = indices. 0 = "aun no creado"

static std::vector<VertexSquare> g_canvas;   // El LIENZO: lo que ya esta dibujado y se queda (permanente)
static std::vector<VertexSquare> g_view;     // Lienzo + vista previa de la figura que se esta arrastrando: es lo que se manda a la GPU
static std::vector<GLuint> g_gridIndices;    // Indices: dicen que 3 vertices forman cada triangulo

static const int g_rows = 100;               // Numero de filas de la grilla
static const int g_cols = 100;               // Numero de columnas de la grilla
static const float SQUARE_SIZE_FACTOR = 0.9f;   // Fraccion de la celda que ocupa el cuadrado: 0.9 deja un borde (se ve la grilla), 1.0 = sin espacios

static float g_baseColor[4] = { 0.5f, 0.2f, 0.7f, 1.0f };   // Color de fondo del lienzo: morado (r,g,b,a). El borrador pinta con este color
static float g_drawColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };   // Color con el que se dibuja (empieza en blanco y se cambia desde ImGui)

// Herramientas disponibles. El enum les da un numero a cada una (0,1,2...) y el ultimo (H_COUNT) es la cantidad total
enum { H_LAPIZ, H_LINEA, H_RECT, H_TRIANGULO, H_POLIGONO, H_POLILIBRE, H_CIRCULO, H_BORRADOR, H_COUNT };
static const char* g_nombresHerramientas[H_COUNT] = {   // Nombres que se muestran en el panel; MISMO ORDEN que el enum
    "Lapiz (a mano alzada)", "Linea", "Rectangulo", "Triangulo",   // Herramientas 0 a 3
    "Poligono regular", "Poligono libre", "Circulo", "Borrador"    // Herramientas 4 a 7
};                                                                 // Fin del arreglo de nombres

static int g_tool = H_LINEA;           // Herramienta activa (empieza en Linea)
static int g_sides = 5;                // Cantidad de lados del poligono regular (empieza en pentagono)
static std::vector<Punto> g_polyPts;   // Vertices del poligono libre (se agregan con cada clic)

static std::vector<std::vector<float>> g_undo;   // Pila para deshacer: cada elemento es una "foto" de los colores del lienzo

// Ultima LINEA o CIRCULO dibujado. Se guarda con sus coordenadas EXACTAS (con decimales) para poder
// girarlo varias veces sin acumular errores de redondeo (45 + 45 queda exactamente en 90)
struct FiguraGirable {                  // Datos de la figura que se puede girar
    bool valida;                        // true si hay una linea/circulo reciente que se puede girar
    int tipo;                           // Que figura es: H_LINEA o H_CIRCULO
    float x0, y0;                       // Linea: primer extremo | Circulo: centro
    float x1, y1;                       // Linea: segundo extremo | Circulo: no se usa
    float radio;                        // Circulo: radio en celdas | Linea: no se usa
    float color[4];                     // Color con el que se dibujo (para redibujarla igual aunque despues cambies el color)
    std::vector<float> fotoAntes;       // Foto del lienzo ANTES de dibujar la figura (sirve para borrarla y redibujarla girada)
};                                      // Fin de la estructura
static FiguraGirable g_ultima = { false, H_LINEA, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,{ 1.0f, 1.0f, 1.0f, 1.0f },{} };   // Al empezar no hay ninguna figura para girar
static bool g_girarSobreSuCentro = false;   // false: gira alrededor del CENTRO DEL LIENZO | true: gira alrededor del centro de la propia figura

// ---------------------------------------------------------------------
//  GENERACION DE LA GRILLA DE CUADRADOS
//  Cada celda = 4 vertices + 6 indices (2 triangulos)
//  Celda (cx,cy) -> sus vertices estan en [(cy*cols+cx)*4 ... +3]
// ---------------------------------------------------------------------
void generatePixelGrid(std::vector<VertexSquare>& outVerts, std::vector<GLuint>& outIndices,   // Parametros de salida por referencia (&): la funcion modifica los vectores originales
    int rows, int cols, const float color[4])                                                    // Filas, columnas y color inicial de todas las celdas
{                                                                                                // Abre el cuerpo de la funcion
    outVerts.clear();                                                 // Vacia la lista de vertices (por si ya tenia datos)
    outIndices.clear();                                               // Vacia la lista de indices
    outVerts.reserve(static_cast<size_t>(cols) * rows * 4);           // Reserva memoria para todos los vertices de una vez (mas rapido). static_cast evita desbordar el int
    outIndices.reserve(static_cast<size_t>(cols) * rows * 6);         // Reserva memoria para los indices (6 por cuadrado)

    float cellWidth = 2.0f / cols;     // Ancho de una celda: OpenGL va de -1 a 1 (ancho total 2), asi que 2/cols
    float cellHeight = 2.0f / rows;    // Alto de una celda: 2/rows

    for (int r = 0; r < rows; ++r) {          // Recorre cada FILA, de abajo (0) hacia arriba
        for (int c = 0; c < cols; ++c) {      // Recorre cada COLUMNA de la fila, de izquierda a derecha
            float cx = -1.0f + (c + 0.5f) * cellWidth;     // Centro X de la celda: parte de -1 (borde izquierdo) y suma (c + media celda)
            float cy = -1.0f + (r + 0.5f) * cellHeight;    // Centro Y de la celda: parte de -1 (borde inferior)
            float halfW = cellWidth * 0.5f * SQUARE_SIZE_FACTOR;    // Mitad del ancho del cuadrado (achicada por el factor para dejar borde)
            float halfH = cellHeight * 0.5f * SQUARE_SIZE_FACTOR;   // Mitad del alto del cuadrado

            VertexSquare v0 = { { cx + halfW, cy + halfH, 0.0f },{ color[0], color[1], color[2], color[3] } };  // Vertice 0: esquina superior derecha (z = 0 porque es 2D)
            VertexSquare v1 = { { cx + halfW, cy - halfH, 0.0f },{ color[0], color[1], color[2], color[3] } };  // Vertice 1: esquina inferior derecha
            VertexSquare v2 = { { cx - halfW, cy - halfH, 0.0f },{ color[0], color[1], color[2], color[3] } };  // Vertice 2: esquina inferior izquierda
            VertexSquare v3 = { { cx - halfW, cy + halfH, 0.0f },{ color[0], color[1], color[2], color[3] } };  // Vertice 3: esquina superior izquierda

            GLuint base = static_cast<GLuint>(outVerts.size());   // Indice del primer vertice de ESTE cuadrado = cantidad de vertices guardados hasta ahora
            outVerts.push_back(v0);                               // Guarda v0 al final de la lista (posicion base+0)
            outVerts.push_back(v1);                               // Guarda v1 (base+1)
            outVerts.push_back(v2);                               // Guarda v2 (base+2)
            outVerts.push_back(v3);                               // Guarda v3 (base+3)

            outIndices.push_back(base + 0);   // Triangulo 1, vertice 1: superior derecha
            outIndices.push_back(base + 1);   // Triangulo 1, vertice 2: inferior derecha
            outIndices.push_back(base + 2);   // Triangulo 1, vertice 3: inferior izquierda
            outIndices.push_back(base + 0);   // Triangulo 2, vertice 1: superior derecha
            outIndices.push_back(base + 2);   // Triangulo 2, vertice 2: inferior izquierda
            outIndices.push_back(base + 3);   // Triangulo 2, vertice 3: superior izquierda (los 2 triangulos forman el cuadrado)
        }                                     // Fin del bucle de columnas
    }                                         // Fin del bucle de filas
}                                             // Fin de la funcion

// ---------------------------------------------------------------------
//  "ENCENDER" UNA CELDA: es el "pixel" que usan TODOS los algoritmos
// ---------------------------------------------------------------------
void setCellColor(std::vector<VertexSquare>& verts, int cols, int rows,   // Lista de vertices (por referencia) y tamano de la grilla
    int cx, int cy, const float color[4])                                  // Celda (cx = columna, cy = fila) a pintar y el color nuevo
{                                                                          // Abre el cuerpo de la funcion
    if (cx < 0 || cx >= cols || cy < 0 || cy >= rows) return;              // Si la celda esta FUERA de la grilla, sale sin hacer nada (evita escribir fuera de memoria y recorta las figuras en el borde)
    size_t cellIndex = static_cast<size_t>(cy) * cols + static_cast<size_t>(cx);   // Numero de celda = fila * columnas + columna (las celdas se guardaron fila por fila)
    size_t base = cellIndex * 4;                                           // Cada celda tiene 4 vertices, asi que su primer vertice esta en cellIndex * 4
    for (size_t i = 0; i < 4; ++i) {                                       // Recorre los 4 vertices de la celda
        verts[base + i].color[0] = color[0];                               // Cambia el componente rojo del vertice
        verts[base + i].color[1] = color[1];                               // Cambia el componente verde
        verts[base + i].color[2] = color[2];                               // Cambia el componente azul
        verts[base + i].color[3] = color[3];                               // Cambia el componente alfa (transparencia)
    }                                                                      // Fin del for: los 4 vertices tienen el color nuevo = todo el cuadrado
}                                                                          // Fin de la funcion

// ---------------------------------------------------------------------
//  LINEA DE BRESENHAM - VERSION HORIZONTAL (pendientes suaves)
//  Se usa cuando la linea avanza MAS en X que en Y (|dy| < |dx|)
//  Da un paso por cada COLUMNA (eje mayor = X) y decide si sube/baja una fila
// ---------------------------------------------------------------------
void LineBresenhamH(std::vector<VertexSquare>& verts, int cols, int rows,   // Lista de vertices y tamano de la grilla
    int x0, int y0, int x1, int y1, const float color[4])                    // Punto inicial (x0,y0), punto final (x1,y1) y color
{                                                                            // Abre el cuerpo de la funcion
    if (x0 > x1) {                          // Si el punto inicial esta mas a la derecha que el final...
        std::swap(x0, x1);                  // ...intercambia x0 y x1
        std::swap(y0, y1);                  // ...y tambien y0 e y1 (para no deformar la linea)
    }                                       // Asi SIEMPRE se recorre de izquierda a derecha (x0 <= x1)
    int dx = x1 - x0;                       // Distancia horizontal (positiva o cero gracias al swap)
    int dy = y1 - y0;                       // Distancia vertical (puede ser positiva, negativa o cero)
    int dir = (dy < 0) ? -1 : 1;            // Direccion vertical: -1 si la linea baja, +1 si sube o es horizontal
    dy *= dir;                              // Convierte dy a valor absoluto (multiplica por -1 si era negativo)
    if (dx != 0) {                          // Evita problemas si x0 == x1 (en ese caso la linea vertical la hace la version V)
        int y = y0;                         // Fila actual: empieza en y0
        int p = 2 * dy - dx;                // Parametro de decision inicial (formula estandar de Bresenham)
        for (int i = 0; i <= dx; ++i) {     // Recorre cada columna desde x0 hasta x1 (dx + 1 columnas en total)
            setCellColor(verts, cols, rows, x0 + i, y, color);   // Pinta la celda actual: columna x0+i, fila y
            if (p >= 0) {                   // Si el punto medio cae mas cerca de la fila siguiente...
                y += dir;                   // ...sube o baja una fila (segun la direccion)
                p -= 2 * dx;                // ...y ajusta el parametro restando 2*dx
            }                               // (si p < 0 la fila no cambia y no se hace nada aqui)
            p += 2 * dy;                    // En ambos casos suma 2*dy al parametro (se avanzo una columna)
        }                                   // Fin del for: se recorrio toda la linea columna por columna
    }                                       // Fin del if (dx != 0)
}                                           // Fin de la funcion

// ---------------------------------------------------------------------
//  LINEA DE BRESENHAM - VERSION VERTICAL (pendientes empinadas)
//  Se usa cuando la linea avanza MAS en Y que en X (|dy| >= |dx|)
//  Da un paso por cada FILA (eje mayor = Y). Es el "espejo" de la version H: intercambia X con Y
// ---------------------------------------------------------------------
void LineBresenhamV(std::vector<VertexSquare>& verts, int cols, int rows,   // Lista de vertices y tamano de la grilla
    int x0, int y0, int x1, int y1, const float color[4])                    // Punto inicial, punto final y color
{                                                                            // Abre el cuerpo de la funcion
    if (y0 > y1) {                          // Si el punto inicial esta mas arriba que el final...
        std::swap(x0, x1);                  // ...intercambia x0 y x1
        std::swap(y0, y1);                  // ...y tambien y0 e y1
    }                                       // Asi SIEMPRE se recorre de abajo hacia arriba (y0 <= y1)
    int dx = x1 - x0;                       // Distancia horizontal (puede ser positiva, negativa o cero)
    int dy = y1 - y0;                       // Distancia vertical (positiva o cero gracias al swap)
    int dir = (dx < 0) ? -1 : 1;            // Direccion horizontal: -1 si va hacia la izquierda, +1 si va a la derecha o es vertical
    dx *= dir;                              // Convierte dx a valor absoluto
    if (dy != 0) {                          // Evita problemas si y0 == y1
        int x = x0;                         // Columna actual: empieza en x0
        int p = 2 * dx - dy;                // Parametro de decision inicial (igual que en H pero con los roles invertidos)
        for (int i = 0; i <= dy; ++i) {     // Recorre cada fila desde y0 hasta y1 (dy + 1 filas en total)
            setCellColor(verts, cols, rows, x, y0 + i, color);   // Pinta la celda actual: columna x, fila y0+i
            if (p >= 0) {                   // Si el punto medio cae mas cerca de la columna siguiente...
                x += dir;                   // ...avanza una columna (segun la direccion)
                p -= 2 * dy;                // ...y ajusta el parametro restando 2*dy
            }                               // (si p < 0 la columna no cambia)
            p += 2 * dx;                    // En ambos casos suma 2*dx (se avanzo una fila)
        }                                   // Fin del for: se recorrio toda la linea fila por fila
    }                                       // Fin del if (dy != 0)
}                                           // Fin de la funcion

// ---------------------------------------------------------------------
//  LINEA DE BRESENHAM - ENVOLTORIO (wrapper)
//  Igual que "LineBresenham" del taller, pero SIN trasladar al centro:
//  recibe directamente celdas de la grilla (0..cols-1, 0..rows-1), que es lo que entrega el mouse.
//  Elige la version H o V segun la pendiente (asi no quedan lineas "punteadas" con pendientes > 1)
// ---------------------------------------------------------------------
void PaintLineBresenham(std::vector<VertexSquare>& verts, int cols, int rows,   // Lista de vertices y tamano de la grilla
    int x0, int y0, int x1, int y1, const float color[4])                        // Puntos (en celdas de la grilla) y color
{                                                                                // Abre el cuerpo de la funcion
    if (x0 == x1 && y0 == y1) {                                // Si inicio y fin son el MISMO punto, la "linea" es un solo pixel
        setCellColor(verts, cols, rows, x0, y0, color);        // Pinta esa unica celda
        return;                                                // Termina la funcion (las versiones H y V no pintarian nada en este caso)
    }                                                          // Fin del if
    if (std::abs(y1 - y0) < std::abs(x1 - x0))                 // Compara distancias: si Y cambia MENOS que X...
        LineBresenhamH(verts, cols, rows, x0, y0, x1, y1, color);   // ...pendiente suave: usa la version horizontal
    else                                                       // Si Y cambia igual o mas que X...
        LineBresenhamV(verts, cols, rows, x0, y0, x1, y1, color);   // ...pendiente empinada (o vertical): usa la version vertical
}                                                              // Fin de la funcion

// ---------------------------------------------------------------------
//  CIRCULO DE BRESENHAM (ALGORITMO DEL PUNTO MEDIO)
//  Un circulo es simetrico en 8 partes (octantes). Se calcula solo 1 octante
//  y cada punto se refleja a los otros 7.
// ---------------------------------------------------------------------
void plotCirclePoints(std::vector<VertexSquare>& verts, int cols, int rows,   // Lista de vertices y tamano de la grilla
    int xc, int yc, int x, int y, const float color[4])                        // Centro (xc,yc), punto calculado (x,y) RELATIVO al centro, y color
{                                                                              // Abre el cuerpo de la funcion
    setCellColor(verts, cols, rows, xc + x, yc + y, color);   // Octante 1: (+x, +y)
    setCellColor(verts, cols, rows, xc - x, yc + y, color);   // Octante 2: (-x, +y) reflejo horizontal
    setCellColor(verts, cols, rows, xc + x, yc - y, color);   // Octante 3: (+x, -y) reflejo vertical
    setCellColor(verts, cols, rows, xc - x, yc - y, color);   // Octante 4: (-x, -y) reflejo en ambos ejes
    setCellColor(verts, cols, rows, xc + y, yc + x, color);   // Octante 5: intercambia x con y (reflejo en la diagonal)
    setCellColor(verts, cols, rows, xc - y, yc + x, color);   // Octante 6
    setCellColor(verts, cols, rows, xc + y, yc - x, color);   // Octante 7
    setCellColor(verts, cols, rows, xc - y, yc - x, color);   // Octante 8
}                                                              // Fin de la funcion

void PaintCircleBresenham(std::vector<VertexSquare>& verts, int cols, int rows,   // Lista de vertices y tamano de la grilla
    int xc, int yc, int r, const float color[4])                                   // Centro (xc,yc), radio r (en celdas) y color
{                                                                                  // Abre el cuerpo de la funcion
    int x = 0;            // Empieza en x = 0 (relativo al centro)
    int y = r;            // y = radio: el punto mas alto del circulo (justo arriba del centro)
    int d = 3 - 2 * r;    // Variable de decision inicial: indica si el punto medio entre dos candidatos esta dentro o fuera del circulo
    while (y >= x) {      // Solo se recorre un octante: desde el punto mas alto hasta la diagonal (donde x alcanza a y)
        plotCirclePoints(verts, cols, rows, xc, yc, x, y, color);   // Pinta este punto y sus 7 simetricos
        x++;              // En cada paso SIEMPRE avanza una celda en x
        if (d > 0) {      // d > 0: el punto medio esta FUERA del circulo, hay que bajar una fila
            y--;                              // Baja una fila (y disminuye)
            d = d + 4 * (x - y) + 10;         // Actualiza la decision considerando que x subio y y bajo
        }                                     // Fin del if
        else {            // d <= 0: el punto medio esta DENTRO del circulo, se queda en la misma fila
            d = d + 4 * x + 6;                // Actualiza la decision considerando solo que x subio
        }                                     // Fin del else
    }                                         // Fin del while: el circulo completo ya quedo dibujado por simetria
}                                             // Fin de la funcion

// ---------------------------------------------------------------------
//  FIGURAS GEOMETRICAS HECHAS CON LINEAS DE BRESENHAM
//  Idea: una figura es un conjunto de vertices unidos por lineas
// ---------------------------------------------------------------------

// Rectangulo: 4 lineas entre las esquinas opuestas (x0,y0) y (x1,y1)
void PaintRectangle(std::vector<VertexSquare>& verts, int cols, int rows,   // Lista de vertices y tamano de la grilla
    int x0, int y0, int x1, int y1, const float color[4])                    // Dos esquinas opuestas y color
{                                                                            // Abre el cuerpo de la funcion
    PaintLineBresenham(verts, cols, rows, x0, y0, x1, y0, color);   // Lado horizontal en la fila y0 (de x0 a x1)
    PaintLineBresenham(verts, cols, rows, x1, y0, x1, y1, color);   // Lado vertical en la columna x1 (de y0 a y1)
    PaintLineBresenham(verts, cols, rows, x1, y1, x0, y1, color);   // Lado horizontal en la fila y1 (de x1 a x0)
    PaintLineBresenham(verts, cols, rows, x0, y1, x0, y0, color);   // Lado vertical en la columna x0 (de y1 a y0)
}                                                                    // Fin de la funcion

// Triangulo isosceles: la base esta en la fila y0 (entre x0 y x1) y la punta en (centro, y1)
// Si arrastras hacia arriba apunta arriba; si arrastras hacia abajo apunta abajo
void PaintTriangle(std::vector<VertexSquare>& verts, int cols, int rows,   // Lista de vertices y tamano de la grilla
    int x0, int y0, int x1, int y1, const float color[4])                   // Punto donde empezo el arrastre, punto donde termino y color
{                                                                           // Abre el cuerpo de la funcion
    int xmin = std::min(x0, x1);                                            // Extremo izquierdo de la base
    int xmax = std::max(x0, x1);                                            // Extremo derecho de la base
    int xm = (xmin + xmax) / 2;                                             // Columna central de la base: ahi va la punta
    PaintLineBresenham(verts, cols, rows, xmin, y0, xmax, y0, color);       // Lado 1: la base (linea horizontal)
    PaintLineBresenham(verts, cols, rows, xmax, y0, xm, y1, color);         // Lado 2: del extremo derecho de la base hasta la punta
    PaintLineBresenham(verts, cols, rows, xm, y1, xmin, y0, color);         // Lado 3: de la punta hasta el extremo izquierdo de la base
}                                                                           // Fin de la funcion

// Poligono regular de n lados: el centro es (xc,yc) y el primer vertice es el punto (xr,yr)
// (la distancia centro-cursor es el radio y la direccion del arrastre gira la figura)
void PaintRegularPolygon(std::vector<VertexSquare>& verts, int cols, int rows,   // Lista de vertices y tamano de la grilla
    int xc, int yc, int xr, int yr, int sides, const float color[4])             // Centro, punto del primer vertice, cantidad de lados y color
{                                                                                // Abre el cuerpo de la funcion
    const float DOS_PI = 6.2831853f;     // Una vuelta completa en radianes (2 * pi)
    float r = std::sqrt(static_cast<float>((xr - xc) * (xr - xc) + (yr - yc) * (yr - yc)));   // Radio = distancia del centro al cursor (teorema de Pitagoras)
    float a0 = std::atan2(static_cast<float>(yr - yc), static_cast<float>(xr - xc));          // Angulo del primer vertice respecto al centro

    int px = xr, py = yr;                // Vertice anterior: se empieza en el primer vertice (el cursor)
    for (int k = 1; k <= sides; ++k) {   // Un paso por cada lado del poligono
        float a = a0 + DOS_PI * k / sides;                                     // Angulo del siguiente vertice: se avanza 2*pi/sides cada vez
        int nx = static_cast<int>(std::lround(xc + r * std::cos(a)));          // Columna del siguiente vertice (coordenada polar -> cartesiana), redondeada a entero
        int ny = static_cast<int>(std::lround(yc + r * std::sin(a)));          // Fila del siguiente vertice
        PaintLineBresenham(verts, cols, rows, px, py, nx, ny, color);          // Dibuja el lado entre el vertice anterior y el nuevo
        px = nx;                         // El vertice nuevo pasa a ser el anterior para la siguiente vuelta
        py = ny;                         // (idem para la fila)
    }                                    // Fin del for: el ultimo lado vuelve al primer vertice y la figura queda cerrada
}                                        // Fin de la funcion

// Poligono libre: une los puntos en orden; si closeIt es true tambien une el ultimo con el primero
void PaintFreePolygon(std::vector<VertexSquare>& verts, int cols, int rows,   // Lista de vertices y tamano de la grilla
    const std::vector<Punto>& pts, bool closeIt, const float color[4])         // Lista de vertices del poligono, si se cierra o no, y color
{                                                                              // Abre el cuerpo de la funcion
    if (pts.empty()) return;                                                   // Si no hay ningun punto no hay nada que dibujar
    for (size_t i = 0; i + 1 < pts.size(); ++i)                                // Recorre los puntos de dos en dos: (0,1), (1,2), (2,3)...
        PaintLineBresenham(verts, cols, rows, pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y, color);   // Une el punto i con el siguiente
    if (closeIt && pts.size() >= 3)                                            // Si hay que cerrar y hay al menos 3 puntos (con menos no es un poligono)...
        PaintLineBresenham(verts, cols, rows, pts.back().x, pts.back().y, pts.front().x, pts.front().y, color);   // ...une el ultimo punto con el primero
}                                                                              // Fin de la funcion

// ---------------------------------------------------------------------
//  DIBUJAR LA FIGURA DE LA HERRAMIENTA ACTUAL (arrastre de (x0,y0) a (x1,y1))
//  Se usa para la VISTA PREVIA (en g_view) y para CONFIRMAR la figura (en g_canvas)
// ---------------------------------------------------------------------
int RadioDe(int x0, int y0, int x1, int y1)   // Radio del circulo = distancia entre el centro (x0,y0) y el cursor (x1,y1)
{                                             // Abre el cuerpo de la funcion
    double dx = static_cast<double>(x1 - x0);   // Diferencia horizontal entre el centro y el cursor
    double dy = static_cast<double>(y1 - y0);   // Diferencia vertical entre el centro y el cursor
    return static_cast<int>(std::lround(std::sqrt(dx * dx + dy * dy)));   // Pitagoras: raiz(dx^2 + dy^2), redondeado a entero
}                                             // Fin de la funcion

bool esHerramientaDeFigura(int tool)   // Devuelve true si la herramienta dibuja una figura al arrastrar y soltar
{                                      // Abre el cuerpo de la funcion
    return tool == H_LINEA || tool == H_RECT || tool == H_TRIANGULO || tool == H_POLIGONO || tool == H_CIRCULO;   // Lapiz, borrador y poligono libre funcionan distinto, por eso no estan aqui
}                                      // Fin de la funcion

void DrawShape(std::vector<VertexSquare>& verts, int tool,         // Lista de vertices donde dibujar y herramienta a usar
    int x0, int y0, int x1, int y1, const float color[4])           // Celda inicial del arrastre, celda actual y color
{                                                                   // Abre el cuerpo de la funcion
    switch (tool) {                                                 // Segun la herramienta elegida...
    case H_LINEA:                                                   // ...si es Linea:
        PaintLineBresenham(verts, g_cols, g_rows, x0, y0, x1, y1, color);                    // linea de Bresenham entre los dos puntos
        break;                                                      // Sale del switch
    case H_RECT:                                                    // ...si es Rectangulo:
        PaintRectangle(verts, g_cols, g_rows, x0, y0, x1, y1, color);                        // 4 lineas entre las esquinas
        break;                                                      // Sale del switch
    case H_TRIANGULO:                                               // ...si es Triangulo:
        PaintTriangle(verts, g_cols, g_rows, x0, y0, x1, y1, color);                         // 3 lineas: base y dos lados
        break;                                                      // Sale del switch
    case H_POLIGONO:                                                // ...si es Poligono regular:
        PaintRegularPolygon(verts, g_cols, g_rows, x0, y0, x1, y1, g_sides, color);          // n lineas entre vertices equidistantes del centro
        break;                                                      // Sale del switch
    case H_CIRCULO: {                                               // ...si es Circulo (las llaves { } permiten declarar variables dentro del case):
        int radio = RadioDe(x0, y0, x1, y1);                        // Radio = distancia entre el centro y el cursor
        PaintCircleBresenham(verts, g_cols, g_rows, x0, y0, radio, color);         // Circulo de Bresenham con ese radio
        break;                                                      // Sale del switch
    }                                                               // Fin del bloque del case H_CIRCULO
    default:                                                        // Cualquier otra herramienta (lapiz, borrador...)
        break;                                                      // No hace nada: esas se manejan en el bucle principal
    }                                                               // Fin del switch
}                                                                   // Fin de la funcion

// ---------------------------------------------------------------------
//  DESHACER: se guarda solo el COLOR de cada celda (mucho mas liviano que guardar todos los vertices)
// ---------------------------------------------------------------------
void TomarFoto(std::vector<float>& foto)   // Copia los colores de TODAS las celdas del lienzo dentro de "foto"
{                                          // Abre el cuerpo de la funcion
    foto.resize(static_cast<size_t>(g_cols) * g_rows * 4);               // Deja el arreglo con 4 numeros (r,g,b,a) por cada celda
    for (size_t i = 0; i < static_cast<size_t>(g_cols) * g_rows; ++i)    // Recorre todas las celdas
        for (int k = 0; k < 4; ++k)                                      // Recorre los 4 componentes del color (r,g,b,a)
            foto[i * 4 + k] = g_canvas[i * 4].color[k];                  // Copia el color del primer vertice de la celda i (los 4 vertices tienen el mismo)
}                                          // Fin de la funcion

void RestaurarFoto(const std::vector<float>& foto)   // Pinta el lienzo con los colores guardados en "foto"
{                                                    // Abre el cuerpo de la funcion
    for (int cy = 0; cy < g_rows; ++cy)                  // Recorre todas las filas
        for (int cx = 0; cx < g_cols; ++cx) {            // Recorre todas las columnas
            size_t i = static_cast<size_t>(cy) * g_cols + cx;                       // Numero de la celda (fila * columnas + columna)
            setCellColor(g_canvas, g_cols, g_rows, cx, cy, &foto[i * 4]);           // Restaura el color que tenia esa celda en la foto
        }                                                // Fin del bucle de columnas
}                                                    // Fin de la funcion

void PushUndo()   // Guarda una "foto" del lienzo ANTES de hacer un cambio
{                 // Abre el cuerpo de la funcion
    g_ultima.valida = false;                             // Cualquier cambio nuevo deja de permitir girar la figura anterior (si lo nuevo es una linea o circulo, se vuelve a activar)
    if (g_undo.size() >= 50) g_undo.erase(g_undo.begin());   // Maximo 50 pasos: si ya hay 50, borra la foto mas antigua
    std::vector<float> foto;                             // Foto nueva (vacia por ahora)
    TomarFoto(foto);                                     // La llena con los colores actuales del lienzo
    g_undo.push_back(foto);                              // Agrega la foto al final de la pila
}                                                        // Fin de la funcion

void Undo()   // Vuelve al estado de la ultima foto guardada
{             // Abre el cuerpo de la funcion
    if (g_undo.empty()) return;                          // Si no hay fotos guardadas, no hay nada que deshacer
    RestaurarFoto(g_undo.back());                        // Pinta el lienzo con los colores de la ultima foto
    g_undo.pop_back();                                   // Elimina esa foto de la pila (ya se uso)
    g_ultima.valida = false;                             // Ya no hay una figura reciente que girar
}                                                        // Fin de la funcion

void LimpiarLienzo()   // Borra todo el dibujo
{                      // Abre el cuerpo de la funcion
    PushUndo();        // Guarda una foto primero, para poder deshacer el borrado
    for (int cy = 0; cy < g_rows; ++cy)                  // Recorre todas las filas
        for (int cx = 0; cx < g_cols; ++cx)              // Recorre todas las columnas
            setCellColor(g_canvas, g_cols, g_rows, cx, cy, g_baseColor);   // Pinta cada celda con el color de fondo
}                                                        // Fin de la funcion

void CerrarPoligonoLibre()   // Confirma el poligono libre que se estaba armando
{                            // Abre el cuerpo de la funcion
    if (g_polyPts.size() >= 2) {                         // Con 2 o mas puntos ya hay algo que dibujar
        PushUndo();                                      // Guarda una foto antes de dibujar
        PaintFreePolygon(g_canvas, g_cols, g_rows, g_polyPts, true, g_drawColor);   // Dibuja los lados y cierra la figura (ultimo vertice con el primero)
    }                                                    // Fin del if
    g_polyPts.clear();                                   // Vacia la lista de vertices para empezar otro poligono
}                                                        // Fin de la funcion

// ---------------------------------------------------------------------
//  GIRAR LA ULTIMA LINEA O CIRCULO
//  Rotacion de un punto (x,y) un angulo t alrededor de un pivote (px,py), con la matriz de rotacion:
//      x' = px + (x - px) * cos(t) - (y - py) * sin(t)
//      y' = py + (x - px) * sin(t) + (y - py) * cos(t)
//  Linea  -> se giran sus 2 extremos y se redibuja con Bresenham.
//  Circulo -> se gira su centro (el radio no cambia) y se redibuja con Bresenham.
//  Un angulo positivo gira en sentido ANTIHORARIO (como se ve en pantalla, con Y hacia arriba).
// ---------------------------------------------------------------------
void GirarUltimaFigura(float grados)   // Recibe cuantos grados girar (45 o 90)
{                                      // Abre el cuerpo de la funcion
    if (!g_ultima.valida) return;      // Si no hay una linea/circulo reciente, no hace nada
    PushUndo();                        // Guarda el estado actual para poder deshacer el giro (esto pone valida = false; se reactiva al final)
    RestaurarFoto(g_ultima.fotoAntes); // Borra la figura: deja el lienzo como estaba ANTES de dibujarla

    float px, py;                      // Pivote: el punto alrededor del cual se gira
    if (g_girarSobreSuCentro) {        // Opcion "girar sobre su propio centro":
        if (g_ultima.tipo == H_LINEA) {                  // Linea: el pivote es su punto medio
            px = (g_ultima.x0 + g_ultima.x1) / 2.0f;     // Columna del punto medio
            py = (g_ultima.y0 + g_ultima.y1) / 2.0f;     // Fila del punto medio
        }                                                // Fin del if de la linea
        else {                                           // Circulo: el pivote es su centro (por eso el circulo no cambia)
            px = g_ultima.x0;                            // Columna del centro
            py = g_ultima.y0;                            // Fila del centro
        }                                                // Fin del else
    }                                                    // Fin de la opcion "su propio centro"
    else {                                               // Opcion normal: girar alrededor del centro del lienzo
        px = (g_cols - 1) / 2.0f;                        // Columna central de la grilla (49.5 con 100 columnas)
        py = (g_rows - 1) / 2.0f;                        // Fila central de la grilla (49.5 con 100 filas)
    }                                                    // Fin del else

    float rad = grados * 3.14159265f / 180.0f;           // Pasa los grados a radianes (las funciones cos y sin trabajan en radianes)
    float c = std::cos(rad);                             // Coseno del angulo (se calcula una sola vez)
    float s = std::sin(rad);                             // Seno del angulo

    auto girar = [&](float& x, float& y) {               // Pequena funcion que gira UN punto (x,y) y deja el resultado en las mismas variables
        float dx = x - px;                               // Posicion del punto respecto al pivote (horizontal)
        float dy = y - py;                               // Posicion del punto respecto al pivote (vertical)
        x = px + dx * c - dy * s;                        // Formula de rotacion para x'
        y = py + dx * s + dy * c;                        // Formula de rotacion para y'
        };                                                   // Fin de la funcion interna
    girar(g_ultima.x0, g_ultima.y0);                     // Gira el primer extremo de la linea (o el centro del circulo)
    if (g_ultima.tipo == H_LINEA)                        // Si es una linea...
        girar(g_ultima.x1, g_ultima.y1);                 // ...gira tambien el segundo extremo

    int ix0 = static_cast<int>(std::lround(g_ultima.x0));   // Redondea a celdas enteras (Bresenham trabaja con enteros)
    int iy0 = static_cast<int>(std::lround(g_ultima.y0));   // (idem para la fila)
    if (g_ultima.tipo == H_LINEA) {                      // Si es una linea...
        int ix1 = static_cast<int>(std::lround(g_ultima.x1));   // ...redondea el segundo extremo
        int iy1 = static_cast<int>(std::lround(g_ultima.y1));   // ...(idem)
        PaintLineBresenham(g_canvas, g_cols, g_rows, ix0, iy0, ix1, iy1, g_ultima.color);   // ...la redibuja con Bresenham en su nueva posicion
    }                                                    // Fin del if de la linea
    else {                                               // Si es un circulo...
        PaintCircleBresenham(g_canvas, g_cols, g_rows, ix0, iy0, static_cast<int>(std::lround(g_ultima.radio)), g_ultima.color);   // ...lo redibuja con Bresenham con el centro nuevo y el mismo radio
    }                                                    // Fin del else
    g_ultima.valida = true;                              // La figura sigue siendo la ultima: se puede seguir girando
}                                                        // Fin de la funcion

// ---------------------------------------------------------------------
//  MOUSE -> CELDA DE LA GRILLA
//  La grilla es un cuadrado (alto x alto) en la esquina izquierda de la ventana.
//  Devuelve true si el mouse esta DENTRO de la grilla. La celda siempre queda limitada
//  a 0..cols-1 / 0..rows-1 (asi las figuras no se rompen al llegar al borde).
// ---------------------------------------------------------------------
bool cursorToCell(GLFWwindow* window, int& cx, int& cy)   // Recibe la ventana y devuelve la celda por referencia (cx = columna, cy = fila)
{                                                         // Abre el cuerpo de la funcion
    double mx, my;                                        // Posicion del mouse en pixeles de la ventana
    glfwGetCursorPos(window, &mx, &my);                   // Pide a GLFW la posicion del mouse (el origen (0,0) esta ARRIBA a la izquierda)
    int winW, winH;                                       // Ancho y alto de la ventana
    glfwGetWindowSize(window, &winW, &winH);              // Pide a GLFW el tamano de la ventana
    double lado = static_cast<double>(winH);              // Lado del cuadrado de la grilla = alto de la ventana (800)

    bool dentro = (mx >= 0 && mx < lado && my >= 0 && my < lado);   // true si el mouse esta dentro del cuadrado de la grilla

    cx = static_cast<int>(mx / lado * g_cols);            // Columna: fraccion horizontal (0 a 1) * numero de columnas
    cy = static_cast<int>((lado - my) / lado * g_rows);   // Fila: la ventana cuenta Y hacia ABAJO y la grilla hacia ARRIBA, por eso se usa (lado - my)
    cx = std::max(0, std::min(g_cols - 1, cx));           // Limita la columna al rango valido 0..99
    cy = std::max(0, std::min(g_rows - 1, cy));           // Limita la fila al rango valido 0..99
    return dentro;                                        // Devuelve si el mouse estaba dentro de la grilla
}                                                         // Fin de la funcion

// ---------------------------------------------------------------------
//  BUFFERS DE OPENGL
// ---------------------------------------------------------------------
void InitBuffers()   // Se llama UNA sola vez: crea VAO / VBO / EBO y sube la grilla a la GPU
{                    // Abre el cuerpo de la funcion
    glGenVertexArrays(1, &VAO);          // Crea 1 VAO y guarda su ID en VAO
    glGenBuffers(1, &VBO);               // Crea 1 buffer para los vertices
    glGenBuffers(1, &EBO);               // Crea 1 buffer para los indices

    glBindVertexArray(VAO);              // Activa el VAO: todo lo que se configure abajo queda guardado en el
    glBindBuffer(GL_ARRAY_BUFFER, VBO);  // Activa el VBO como buffer de vertices
    glBufferData(GL_ARRAY_BUFFER, g_view.size() * sizeof(VertexSquare), g_view.data(), GL_DYNAMIC_DRAW);   // Copia los vertices a la GPU. GL_DYNAMIC_DRAW = los datos cambian seguido (cada vez que pintas)
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);   // Activa el EBO como buffer de indices
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, g_gridIndices.size() * sizeof(GLuint), g_gridIndices.data(), GL_STATIC_DRAW);   // Copia los indices a la GPU. GL_STATIC_DRAW = no cambian nunca

    GLsizei stride = sizeof(VertexSquare);   // Stride = bytes que ocupa UN vertice completo (3 + 4 floats = 28 bytes); OpenGL lo usa para saltar al siguiente
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);                       // Atributo 0 (posicion): 3 floats, sin normalizar, empezando en el byte 0
    glEnableVertexAttribArray(0);                                                            // Habilita el atributo 0
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));     // Atributo 1 (color): 4 floats, empezando despues de los 3 floats de la posicion
    glEnableVertexAttribArray(1);                                                            // Habilita el atributo 1
    glBindVertexArray(0);                // Desactiva el VAO (buena practica para no modificarlo por accidente)
}                                        // Fin de la funcion

void UploadView()   // Se llama en CADA fotograma: actualiza en la GPU los colores de los vertices
{                   // Abre el cuerpo de la funcion
    glBindBuffer(GL_ARRAY_BUFFER, VBO);   // Activa el buffer de vertices
    glBufferSubData(GL_ARRAY_BUFFER, 0, g_view.size() * sizeof(VertexSquare), g_view.data());   // Reescribe el contenido (desde el byte 0) sin crear el buffer de nuevo
}                                         // Fin de la funcion

void checkShader(GLuint shader, const char* name)   // Detecta errores al compilar un shader: recibe su ID y su nombre (para el mensaje)
{                                                   // Abre el cuerpo de la funcion
    GLint ok;                                       // Guardara 1 si compilo bien y 0 si fallo
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);  // Consulta a OpenGL el estado de compilacion
    if (!ok) {                                      // Si NO compilo correctamente...
        char log[512];                              // Arreglo de texto para guardar el mensaje de error
        glGetShaderInfoLog(shader, 512, NULL, log); // Pide a OpenGL el mensaje de error (maximo 512 caracteres)
        std::cout << "Error compilando " << name << ":\n" << log << std::endl;   // Lo muestra en la consola
    }                                               // Fin del if
}                                                   // Fin de la funcion

// ---------------------------------------------------------------------
//  PROGRAMA PRINCIPAL
// ---------------------------------------------------------------------
int main()   // Punto de entrada: el programa empieza aqui
{            // Abre el cuerpo de main
    if (!glfwInit()) return -1;                                      // Inicializa GLFW (obligatorio antes de usarla); si falla, termina con error
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);                   // Pide OpenGL version 3.x...
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);                   // ...especificamente 3.3
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);   // Perfil "core": solo funciones modernas (sin las antiguas obsoletas)
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);                      // Ventana de tamano fijo: la grilla (800x800) + el panel a la derecha (300)

    GLFWwindow* window = glfwCreateWindow(1100, 800, "Paint - Bresenham", NULL, NULL);   // Crea la ventana de 1100x800 con ese titulo (los NULL son monitor completo y ventana compartida; no se usan)
    if (window == NULL) {                                            // Si la ventana no se pudo crear...
        std::cout << "Failed to create GLFW window" << std::endl;    // ...muestra el error
        glfwTerminate();                                             // ...cierra GLFW liberando recursos
        return -1;                                                   // ...y termina el programa con codigo de error
    }                                                                // Fin del if
    glfwMakeContextCurrent(window);   // Hace que el contexto OpenGL de esta ventana sea el activo (necesario para dibujar)
    glfwSwapInterval(1);              // Sincroniza con la pantalla (V-Sync): evita gastar GPU dibujando fotogramas de mas

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {       // Carga los punteros de las funciones de OpenGL (debe ir DESPUES de crear el contexto)
        std::cout << "Failed to initialize GLAD" << std::endl;       // Si falla, muestra el error...
        return -1;                                                   // ...y termina el programa
    }                                                                // Fin del if

    // ---- Inicializar ImGui ----
    IMGUI_CHECKVERSION();                       // Verifica que los archivos de ImGui sean de la misma version
    ImGui::CreateContext();                     // Crea el contexto de ImGui (donde guarda todo su estado)
    ImGuiIO& io = ImGui::GetIO();               // Obtiene la estructura de entrada/salida (mouse, teclado, configuracion)
    io.IniFilename = NULL;                      // Evita que ImGui cree el archivo imgui.ini en la carpeta del proyecto
    ImGui::StyleColorsDark();                   // Elige el tema oscuro
    ImGui_ImplGlfw_InitForOpenGL(window, true); // Conecta ImGui con la ventana GLFW (true = instala sus propios callbacks de mouse y teclado)
    ImGui_ImplOpenGL3_Init("#version 330");     // Conecta ImGui con OpenGL usando GLSL 3.30

    // ---- Shaders ----
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);          // Crea un objeto shader de tipo vertice y guarda su ID
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);      // Le asigna el codigo fuente (1 cadena; NULL = terminada en \0)
    glCompileShader(vertexShader);                                   // Lo compila en la GPU
    checkShader(vertexShader, "vertexShader");                       // Verifica que haya compilado sin errores

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);      // Crea un objeto shader de tipo fragmento
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);  // Le asigna su codigo fuente
    glCompileShader(fragmentShader);                                 // Lo compila
    checkShader(fragmentShader, "fragmentShader");                   // Verifica errores

    GLuint shaderProgram = glCreateProgram();                        // Crea un programa (contenedor que une varios shaders)
    glAttachShader(shaderProgram, vertexShader);                     // Agrega el vertex shader al programa
    glAttachShader(shaderProgram, fragmentShader);                   // Agrega el fragment shader al programa
    glLinkProgram(shaderProgram);                                    // Enlaza (une) los shaders en un programa ejecutable
    glDeleteShader(vertexShader);                                    // Borra el vertex shader suelto: ya esta dentro del programa
    glDeleteShader(fragmentShader);                                  // Borra el fragment shader suelto

    // ---- Lienzo ----
    generatePixelGrid(g_canvas, g_gridIndices, g_rows, g_cols, g_baseColor);   // Crea la grilla completa en color de fondo
    g_view = g_canvas;                   // La vista empieza igual que el lienzo
    InitBuffers();                       // Sube la grilla a la GPU por primera vez

    // ---- Estado del mouse ----
    bool dragging = false;               // true mientras se arrastra con el boton izquierdo
    int startX = 0, startY = 0;          // Celda donde EMPEZO el arrastre
    int lastX = 0, lastY = 0;            // Ultima celda visitada (la usan el lapiz y el borrador)

    while (!glfwWindowShouldClose(window)) {                         // Bucle principal: una vuelta = un fotograma; se repite hasta que se cierre la ventana
        glfwPollEvents();                                            // Procesa los eventos pendientes (mouse, teclado, cierre de ventana)
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)       // Si se esta presionando la tecla ESC...
            glfwSetWindowShouldClose(window, true);                  // ...marca la ventana para cerrarse (el while termina en la siguiente vuelta)

        ImGui_ImplOpenGL3_NewFrame();    // Prepara ImGui para un fotograma nuevo (parte de OpenGL)
        ImGui_ImplGlfw_NewFrame();       // Le pasa a ImGui el estado actual del mouse y el teclado
        ImGui::NewFrame();               // Empieza el fotograma de ImGui: desde aqui se pueden crear ventanas y botones

        int winW, winH;                                  // Ancho y alto de la ventana
        glfwGetWindowSize(window, &winW, &winH);         // Los consulta a GLFW
        int cx, cy;                                      // Celda donde esta el mouse
        bool dentro = cursorToCell(window, cx, cy);      // Calcula la celda bajo el mouse; "dentro" dice si esta sobre la grilla

        // ================= PANEL DE IMGUI (a la derecha) =================
        ImGui::SetNextWindowPos(ImVec2(static_cast<float>(winH) + 10.0f, 10.0f), ImGuiCond_Always);   // Posiciona el panel justo a la derecha de la grilla (siempre en el mismo sitio)
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(winW - winH) - 20.0f, static_cast<float>(winH) - 20.0f), ImGuiCond_Always);   // Tamano del panel: el espacio sobrante a la derecha menos un margen
        ImGui::Begin("Herramientas", NULL, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);   // Abre la ventana del panel (sin poder moverla, cambiarle el tamano ni colapsarla)

        for (int i = 0; i < H_COUNT; ++i)                                // Recorre todas las herramientas
            ImGui::RadioButton(g_nombresHerramientas[i], &g_tool, i);    // Dibuja un boton de opcion; si se hace clic, g_tool pasa a valer i

        ImGui::Separator();                                              // Linea separadora
        if (g_tool == H_POLIGONO) {                                      // Si la herramienta es Poligono regular...
            ImGui::SliderInt("Lados", &g_sides, 3, 12);                  // ...muestra un deslizador para elegir de 3 a 12 lados
            ImGui::TextWrapped("Arrastra desde el centro. La direccion del arrastre gira la figura.");   // ...y una ayuda
        }                                                                // Fin del if
        else if (g_tool == H_POLILIBRE) {                                // Si es Poligono libre...
            ImGui::TextWrapped("Clic izquierdo: agrega un vertice. Clic derecho o Enter: cierra el poligono.");   // ...muestra la ayuda
            if (ImGui::Button("Cerrar poligono")) CerrarPoligonoLibre(); // ...boton para cerrar y dibujar el poligono
            ImGui::SameLine();                                           // ...el siguiente boton va en la misma linea
            if (ImGui::Button("Cancelar")) g_polyPts.clear();            // ...boton para descartar los vertices puestos
        }                                                                // Fin del else if
        else if (g_tool == H_CIRCULO) {                                  // Si es Circulo...
            ImGui::TextWrapped("Arrastra desde el centro: la distancia al cursor es el radio.");   // ...muestra la ayuda
        }                                                                // Fin del else if
        else if (g_tool == H_TRIANGULO) {                                // Si es Triangulo...
            ImGui::TextWrapped("Arrastra: la base queda a la altura donde empiezas y la punta donde sueltas.");   // ...muestra la ayuda
        }                                                                // Fin del else if
        else if (g_tool == H_BORRADOR) {                                 // Si es Borrador...
            ImGui::TextWrapped("Arrastra para borrar (pinta con el color de fondo).");   // ...muestra la ayuda
        }                                                                // Fin del else if
        else {                                                           // Para las demas herramientas (lapiz, linea, rectangulo)...
            ImGui::TextWrapped("Arrastra con el boton izquierdo sobre la grilla.");      // ...muestra la ayuda general
        }                                                                // Fin del else

        ImGui::Separator();                       // Linea separadora
        ImGui::ColorEdit3("Color", g_drawColor);  // Selector de color RGB: modifica directamente g_drawColor

        ImGui::Separator();                                  // Linea separadora
        if (ImGui::Button("Deshacer (Ctrl+Z)")) Undo();      // Boton: vuelve al estado anterior del lienzo
        if (ImGui::Button("Limpiar lienzo")) LimpiarLienzo();// Boton: borra todo el dibujo

        ImGui::Separator();                                  // Linea separadora
        ImGui::TextWrapped("Girar la ultima linea o circulo dibujado (sentido antihorario):");   // Texto de ayuda
        ImGui::BeginDisabled(!g_ultima.valida);              // Deshabilita (gris) los botones si no hay una linea/circulo reciente
        if (ImGui::Button("Girar 45 grados")) GirarUltimaFigura(45.0f);   // Boton: gira 45 grados
        ImGui::SameLine();                                   // El siguiente boton va en la misma linea
        if (ImGui::Button("Girar 90 grados")) GirarUltimaFigura(90.0f);   // Boton: gira 90 grados
        ImGui::EndDisabled();                                // Termina la zona deshabilitada
        ImGui::Checkbox("Girar sobre su propio centro", &g_girarSobreSuCentro);   // Casilla: cambia el pivote del giro
        ImGui::TextWrapped("Sin marcar: gira alrededor del centro del lienzo. Un circulo girado sobre su propio centro no cambia.");   // Aclaracion

        ImGui::Separator();                                  // Linea separadora
        if (dentro) ImGui::Text("Celda: (%d, %d)", cx, cy);  // Si el mouse esta sobre la grilla, muestra la celda (columna, fila)
        else        ImGui::Text("Celda: -");                 // Si no, muestra un guion
        if (dragging && g_tool == H_CIRCULO) {               // Mientras se arrastra un circulo...
            double ddx = static_cast<double>(cx - startX), ddy = static_cast<double>(cy - startY);   // ...diferencias entre el centro y el cursor
            ImGui::Text("Radio: %ld", std::lround(std::sqrt(ddx * ddx + ddy * ddy)));                // ...muestra el radio actual
        }                                                    // Fin del if
        ImGui::End();                                        // Cierra la ventana del panel

        // ================= INTERACCION CON EL MOUSE =================
        bool sobreUI = io.WantCaptureMouse;                  // true si el mouse esta sobre el panel de ImGui (en ese caso NO se debe pintar en la grilla)

        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) Undo();   // Ctrl + Z: deshacer

        const float* colorActual = (g_tool == H_BORRADOR) ? g_baseColor : g_drawColor;   // Color a usar: el borrador usa el color de fondo, las demas el color elegido

        if (g_tool == H_POLILIBRE) {                                         // ----- Poligono libre: cada clic agrega un vertice -----
            dragging = false;                                                // Esta herramienta no usa arrastre
            if (ImGui::IsMouseClicked(0) && dentro && !sobreUI)              // Clic izquierdo sobre la grilla (no sobre el panel)...
                g_polyPts.push_back({ cx, cy });                             // ...agrega la celda como nuevo vertice
            if (!sobreUI && ImGui::IsMouseClicked(1) && !g_polyPts.empty())  // Clic derecho con al menos un vertice...
                CerrarPoligonoLibre();                                       // ...cierra y dibuja el poligono
            if (ImGui::IsKeyPressed(ImGuiKey_Enter) && !g_polyPts.empty())   // Tecla Enter con al menos un vertice...
                CerrarPoligonoLibre();                                       // ...tambien cierra y dibuja el poligono
        }                                                                    // Fin del caso poligono libre
        else {                                                               // ----- Todas las demas herramientas -----
            g_polyPts.clear();                                               // Si se cambio de herramienta, descarta el poligono libre a medias

            // --- Inicio del arrastre ---
            if (ImGui::IsMouseClicked(0) && dentro && !sobreUI) {            // Se acaba de presionar el boton izquierdo sobre la grilla...
                dragging = true;                                             // ...empieza el arrastre
                startX = lastX = cx;                                         // ...recuerda la columna donde empezo
                startY = lastY = cy;                                         // ...y la fila donde empezo
                if (g_tool == H_LAPIZ || g_tool == H_BORRADOR) {             // Si es lapiz o borrador (pintan mientras arrastras)...
                    PushUndo();                                              // ...guarda una foto ANTES de pintar
                    setCellColor(g_canvas, g_cols, g_rows, cx, cy, colorActual);   // ...pinta la primera celda
                }                                                            // Fin del if
            }                                                                // Fin del inicio del arrastre

            // --- Durante el arrastre (y en el fotograma en que se suelta) ---
            if (dragging && (ImGui::IsMouseDown(0) || ImGui::IsMouseReleased(0))) {   // Si se esta arrastrando...
                if ((g_tool == H_LAPIZ || g_tool == H_BORRADOR) && (cx != lastX || cy != lastY)) {   // ...y es lapiz/borrador y el mouse cambio de celda...
                    PaintLineBresenham(g_canvas, g_cols, g_rows, lastX, lastY, cx, cy, colorActual);  // ...une la celda anterior con la actual con una linea de Bresenham (si no, al mover rapido quedarian huecos)
                    lastX = cx;                                              // ...la celda actual pasa a ser la anterior
                    lastY = cy;                                              // ...(idem para la fila)
                }                                                            // Fin del if interno
            }                                                                // Fin del if de arrastre

            // --- Soltar el boton: se confirma la figura en el lienzo ---
            if (dragging && ImGui::IsMouseReleased(0)) {                     // Se acaba de soltar el boton izquierdo...
                if (esHerramientaDeFigura(g_tool)) {                         // ...y la herramienta dibuja una figura...
                    PushUndo();                                              // ...guarda una foto antes de dibujar (esto tambien pone g_ultima.valida = false)
                    bool girable = (g_tool == H_LINEA || g_tool == H_CIRCULO);   // ...la linea y el circulo se pueden girar despues
                    if (girable) TomarFoto(g_ultima.fotoAntes);              // ...guarda el lienzo SIN la figura (para poder borrarla al girar)
                    DrawShape(g_canvas, g_tool, startX, startY, cx, cy, g_drawColor);   // ...dibuja la figura de forma permanente en el lienzo
                    if (girable) {                                           // ...y si era linea o circulo, la recuerda:
                        g_ultima.tipo = g_tool;                              // ...que figura fue
                        g_ultima.x0 = static_cast<float>(startX);            // ...primer extremo (o centro): columna
                        g_ultima.y0 = static_cast<float>(startY);            // ...primer extremo (o centro): fila
                        g_ultima.x1 = static_cast<float>(cx);                // ...segundo extremo: columna
                        g_ultima.y1 = static_cast<float>(cy);                // ...segundo extremo: fila
                        g_ultima.radio = static_cast<float>(RadioDe(startX, startY, cx, cy));   // ...radio (solo sirve para el circulo)
                        for (int k = 0; k < 4; ++k)                          // ...copia el color usado
                            g_ultima.color[k] = g_drawColor[k];              // ...componente por componente
                        g_ultima.valida = true;                              // ...ahora SI hay una figura que se puede girar
                    }                                                        // Fin del if (girable)
                }                                                            // Fin del if interno
                dragging = false;                                            // ...termina el arrastre
            }                                                                // Fin del soltar
            if (dragging && !ImGui::IsMouseDown(0) && !ImGui::IsMouseReleased(0))   // Seguridad: si el boton ya no esta presionado y no se detecto el soltar...
                dragging = false;                                            // ...cancela el arrastre para no quedarse "pegado"
        }                                                                    // Fin del else (demas herramientas)

        // ================= ARMAR LO QUE SE VE =================
        g_view = g_canvas;                                      // Copia el lienzo permanente: sobre esta copia se dibuja la vista previa
        if (dragging && esHerramientaDeFigura(g_tool))          // Mientras se arrastra una figura...
            DrawShape(g_view, g_tool, startX, startY, cx, cy, g_drawColor);   // ...se dibuja SOLO en la copia (asi la vista previa no deja rastro)
        if (g_tool == H_POLILIBRE && !g_polyPts.empty()) {      // Si hay un poligono libre a medias...
            std::vector<Punto> temp = g_polyPts;                // ...copia sus vertices
            temp.push_back({ cx, cy });                         // ...agrega el cursor como ultimo vertice provisional
            PaintFreePolygon(g_view, g_cols, g_rows, temp, false, g_drawColor);   // ...dibuja los lados (sin cerrar) solo en la copia
        }                                                       // Fin del if

        // ================= DIBUJAR =================
        int fbW, fbH;                                           // Tamano del framebuffer (en pixeles reales; puede diferir del de la ventana en pantallas de alta densidad)
        glfwGetFramebufferSize(window, &fbW, &fbH);             // Lo consulta a GLFW
        glViewport(0, 0, fbH, fbH);                             // Area de dibujo: un CUADRADO fbH x fbH en la esquina inferior izquierda (ahi va la grilla)

        glClearColor(0.34f, 0.53f, 0.87f, 1.0f);                // Color de fondo: azul (r,g,b,a)
        glClear(GL_COLOR_BUFFER_BIT);                           // Borra la pantalla con ese color para empezar un fotograma limpio

        UploadView();                                           // Sube a la GPU los colores actuales (lienzo + vista previa)
        glUseProgram(shaderProgram);                            // Activa nuestros shaders
        glBindVertexArray(VAO);                                 // Activa el VAO con la configuracion de vertices e indices
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(g_gridIndices.size()), GL_UNSIGNED_INT, 0);   // Dibuja: lee los indices de 3 en 3 como triangulos; cantidad = tamano de la lista de indices

        ImGui::Render();                                        // Termina el fotograma de ImGui y prepara lo que debe dibujar
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData()); // Dibuja el panel de ImGui ENCIMA de la grilla

        glfwSwapBuffers(window);                                // Intercambia el buffer oculto (donde se dibujo) con el visible; asi no hay parpadeos
    }                                                           // Fin del bucle principal

    // ---- Limpieza al cerrar ----
    ImGui_ImplOpenGL3_Shutdown();       // Libera los recursos de ImGui en OpenGL
    ImGui_ImplGlfw_Shutdown();          // Libera los recursos de ImGui en GLFW
    ImGui::DestroyContext();            // Destruye el contexto de ImGui

    glDeleteVertexArrays(1, &VAO);      // Libera el VAO de la GPU
    glDeleteBuffers(1, &VBO);           // Libera el buffer de vertices
    glDeleteBuffers(1, &EBO);           // Libera el buffer de indices
    glDeleteProgram(shaderProgram);     // Libera el programa de shaders
    glfwDestroyWindow(window);          // Destruye la ventana
    glfwTerminate();                    // Cierra GLFW y libera todo
    return 0;                           // Termina el programa correctamente (0 = sin errores)
}                                       // Fin de main                                    // Fin de main