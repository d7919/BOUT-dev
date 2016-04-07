/*


 */

#ifndef __DATAITERATOR_H__
#define __DATAITERATOR_H__

#include <iterator>
#include <iostream>

// Set of indices - DataIterator is dereferenced into these
struct Indices {
  int x;
  int y;
  int z;
};

#define DI_GET_END ((void *) NULL)

class DataIterator
  : public std::iterator<std::bidirectional_iterator_tag, Indices> {
public:
  /// Constructor. This would set ranges. Could depend on thread number
  DataIterator(int xs, int xe,
	       int ys, int ye,
	       int zs, int ze) : x(xs), y(ys), z(zs),
				 xstart(xs), xend(xe),
				 ystart(ys), yend(ye),
				 zstart(zs), zend(ze) {
  }

  // set end();
  DataIterator(int xs, int xe,
	       int ys, int ye,
	       int zs, int ze,void* dummy) : x(xe), y(ye), z(ze),
				 xstart(xs), xend(xe),
				 ystart(ys), yend(ye),
				 zstart(zs), zend(ze) {
    next();
  }
  /// The index variables, updated during loop
  // Should make these private and provide getters?
  int x, y, z;

  /// Increment operators
  DataIterator& operator++() { next(); return *this; }
  DataIterator operator++(int) { DataIterator tmp(*this); next(); return tmp; }
  DataIterator& operator--() { prev(); return *this; }
  DataIterator operator--(int) { DataIterator tmp(*this); prev(); return tmp; }

  // Comparison operator
  inline bool operator!=(const DataIterator& rhs) const {
    return (x != rhs.x) || (y != rhs.y) || (z != rhs.z);
  }
  
  /*
   * Dereference operators
   * These are needed because the C++11 for loop
   * dereferences the iterator
   */
  DataIterator& operator*() {
    return *this;
  }
  const DataIterator& operator*() const {
    return *this;
  }

  /*
   * Add an offset to the index for general stencils
   */
  const Indices offset(int dx, int dy, int dz) {
    return {x+dx, y+dy, z+dz};
  }
  /*
   * Shortcuts for common offsets, one cell
   * in each direction.
   */
  const Indices xp() { return {x+1, y, z}; }
  const Indices xm() { return {x-1, y, z}; }
  const Indices yp() { return {x, y+1, z}; }
  const Indices ym() { return {x, y-1, z}; }
  // Z indices should wrap in zstart, zend range (?)
  const Indices zp() { return {x, y, z == zend ? zstart : z+1}; }
  const Indices zm() { return {x, y, z == zstart ? zend : z-1}; }
  
  void start() {
    x = xstart; y = ystart; z = zstart;
  }

  void end() {
    x = xend; y = yend; z = zend;
    next();
  }

  /// Checks if finished looping. Is this more efficient than
  /// using the more idiomatic it != MeshIterator::end() ?
  bool done() const {
    return (x > xend) || (x < xstart);
  }
  
private:
  DataIterator(); // Disable null constructor

  int xstart, xend;
  int ystart, yend;
  int zstart, zend;

  /// Advance to the next index
  void next() {
    ++z;
    if(z > zend) {
      z = zstart;
      ++y;
      if(y > yend) {
	y = ystart;
	++x;
      }
    }
  }

  /// Rewind to the previous index
  void prev() {
    --z;
    if(z < zstart) {
      z = zend;
      --y;
      if(y < ystart) {
	y = yend;
	--x;
      }
    }
  }
  /// This one is dangerours, as the OpenMP implementation assumes all iterators go from start to end.
  DataIterator(int x, int xs, int xe,
	       int y, int ys, int ye,
	       int z, int zs, int ze) : x(x), y(y), z(z),
					xstart(xs), xend(xe),
					ystart(ys), yend(ye),
					zstart(zs), zend(ze) {
					}
};

/*
 * Specifies a range of indices which can be iterated over
 */
struct IndexRange {
  int xstart, xend;
  int ystart, yend;
  int zstart, zend;
  
  const DataIterator begin() const {
    return DataIterator(xstart, xend, 
                        ystart, yend,
                        zstart, zend);
  }
  const DataIterator end() const {
    return DataIterator(xstart, xend, 
			ystart, yend,
			zstart, zend, (void*)NULL);;
  }
};

#endif // __DATAITERATOR_H__