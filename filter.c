#include "filter.h"

void filter_rows(
    const double *erosion_src,
    const double *dilation_src,
    double *erosion_dst,
    double *dilation_dst,
    size_t rows,
    size_t cols,
    size_t begin_row,
    size_t end_row
)
{
    for (size_t row = begin_row; row < end_row; ++row) {
        for (size_t col = 0; col < cols; ++col) {
            size_t index = row * cols + col;

            double minimum = erosion_src[index];
            double maximum = dilation_src[index];

            // окно 3 x 3 по границам матрицы
            size_t first_row = (row == 0) ? 0 : row - 1;
            size_t last_row = (row == rows - 1) ? row : row + 1;
            size_t first_col = (col == 0) ? 0 : col - 1;
            size_t last_col = (col == cols - 1) ? col : col + 1;

            for (size_t r = first_row; r <= last_row; ++r) {
                for (size_t c = first_col; c <= last_col; ++c) {
                    size_t neighbor = r * cols + c;

                    if (erosion_src[neighbor] < minimum) {
                        minimum = erosion_src[neighbor];
                    }

                    if (dilation_src[neighbor] > maximum) {
                        maximum = dilation_src[neighbor];
                    }
                }
            }

            erosion_dst[index] = minimum;
            dilation_dst[index] = maximum;
        }
    }
}