// struct for keeping track of the percent of free physical pages
struct physicalPagesCounts{
  uint totalFreePages;
  uint currentFreePagesNo;
};

extern struct physicalPagesCounts physicalPagesCounts;