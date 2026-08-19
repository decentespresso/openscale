#ifndef HDS_TIMING_H
#define HDS_TIMING_H

inline bool hdsIntervalElapsed(unsigned long now,
                                unsigned long last,
                                unsigned long interval) {
  return now - last >= interval;
}

#endif
