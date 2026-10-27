#ifndef FILTER_H
#define FILTER_H

#include <stddef.h>

//Вычислить строки [begin_row, end_row) двух результатов.
//Входные и выходные буферы не должны пересекаться.
 
void filter_rows(
    const double *erosion_src,
    const double *dilation_src,
    double *erosion_dst,
    double *dilation_dst,
    size_t rows,
    size_t cols,
    size_t begin_row,
    size_t end_row
);

#endif