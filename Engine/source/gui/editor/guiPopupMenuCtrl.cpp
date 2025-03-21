//-----------------------------------------------------------------------------
// Copyright (c) 2012 GarageGames, LLC
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
//-----------------------------------------------------------------------------

#include "gui/editor/guiPopupMenuCtrl.h"
#include "gfx/gfxDrawUtil.h"
#include "gfx/primBuilder.h"
#include "gui/core/guiCanvas.h"

GuiPopupMenuBackgroundCtrl::GuiPopupMenuBackgroundCtrl()
{
   mMenuBarCtrl = nullptr;
}

void GuiPopupMenuBackgroundCtrl::onMouseDown(const GuiEvent &event)
{
   
}

void GuiPopupMenuBackgroundCtrl::onMouseUp(const GuiEvent &event)
{
   clearPopups();

   //Pass along the event just in case we clicked over a menu item. We don't want to eat the input for it.
   if (mMenuBarCtrl)
      mMenuBarCtrl->onMouseUp(event);

   close();
}

void GuiPopupMenuBackgroundCtrl::onMouseMove(const GuiEvent &event)
{
   //It's possible we're trying to pan through a menubar while a popup is displayed. Pass along our event to the menubar for good measure
   if (mMenuBarCtrl)
      mMenuBarCtrl->onMouseMove(event);
}

void GuiPopupMenuBackgroundCtrl::onMouseDragged(const GuiEvent &event)
{
}

void GuiPopupMenuBackgroundCtrl::close()
{
   if(getRoot())
      getRoot()->removeObject(this);

   mMenuBarCtrl = nullptr;
}

S32 GuiPopupMenuBackgroundCtrl::findPopupMenu(PopupMenu* menu)
{
   S32 menuId = -1;

   for (U32 i = 0; i < mPopups.size(); i++)
   {
      if (mPopups[i]->getId() == menu->getId())
         return i;
   }

   return menuId;
}

void GuiPopupMenuBackgroundCtrl::clearPopups()
{
   for (U32 i = 0; i < mPopups.size(); i++)
   {
      mPopups[i]->mTextList->setSelectedCell(Point2I(-1, -1));
      mPopups[i]->mTextList->mPopup->hidePopup();
   }
}

GuiPopupMenuTextListCtrl::GuiPopupMenuTextListCtrl()
{
   isSubMenu = false; //  Added

   mMenuBar = nullptr;
   mPopup = nullptr;

   mLastHighlightedMenuIdx = -1;
   mBackground = NULL;
}

void GuiPopupMenuTextListCtrl::onRenderCell(Point2I offset, Point2I cell, bool selected, bool mouseOver)
{
   //check if we're a real entry, or if it's a divider
   if (mPopup->mMenuItems[cell.y].mIsSpacer)
   {
      S32 yp = offset.y + mCellSize.y / 2;
      GFX->getDrawUtil()->drawLine(offset.x + 5, yp, offset.x + mCellSize.x - 5, yp, ColorI(128, 128, 128));
   }
   else
   {
      if (String::compare(mList[cell.y].text + 3, "-\t")) //  Was: String::compare(mList[cell.y].text + 2, "-\t")) but has been changed to take into account the submenu flag
      {
         Parent::onRenderCell(offset, cell, selected, mouseOver);
      }
      else
      {
         S32 yp = offset.y + mCellSize.y / 2;
         GFX->getDrawUtil()->drawLine(offset.x, yp, offset.x + mCellSize.x, yp, ColorI(128, 128, 128));
         GFX->getDrawUtil()->drawLine(offset.x, yp + 1, offset.x + mCellSize.x, yp + 1, ColorI(255, 255, 255));
      }
   }

   // now see if there's a bitmap...
   U8 idx = mList[cell.y].text[0];
   if (idx != 1)
   {
      // there's a bitmap...
      U32 index = U32(idx - 2) * 4;
      if (!mList[cell.y].active)
         index += 3;
      else if (selected)
         index++;
      else if (mouseOver)
         index += 2;

      if (mProfile->mBitmapArrayRects.size() > index)
      {
         RectI rect = mProfile->mBitmapArrayRects[index];
         Point2I off = maxBitmapSize - rect.extent;
         off /= 2;

         Point2I bitPos = Point2I(offset.x + mCellSize.y / 2, offset.y + mCellSize.y / 2);

         GFX->getDrawUtil()->clearBitmapModulation();
         GFX->getDrawUtil()->drawBitmapSR(mProfile->getBitmapResource(), bitPos + off, rect);
      }
   }

   //  Check if this is a submenu
   idx = mList[cell.y].text[1];
   if (idx != 1)
   {
      // This is a submenu, so draw an arrow
      S32 left = offset.x + mCellSize.x - 12;
      S32 right = left + 8;
      S32 top = mCellSize.y / 2 + offset.y - 4;
      S32 bottom = top + 8;
      S32 middle = top + 4;

      //PrimBuild::begin(GFXTriangleList, 3);

      ColorI color = ColorI::BLACK;
      if (selected || mouseOver)
         color = mProfile->mFontColorHL;
      else
         color = mProfile->mFontColor;

      GFX->getDrawUtil()->drawLine(Point2I(left, top), Point2I(right, middle), color);
      GFX->getDrawUtil()->drawLine(Point2I(right, middle), Point2I(left, bottom), color);
      GFX->getDrawUtil()->drawLine(Point2I(left, bottom), Point2I(left, top), color);

      /*PrimBuild::vertex2i(left, top);
      PrimBuild::vertex2i(right, middle);
      PrimBuild::vertex2i(left, bottom);
      PrimBuild::end();*/
   }

   //check if we're checked
   if (mPopup->mMenuItems[cell.y].mIsChecked)
   {
      GFX->getDrawUtil()->draw2DSquare(Point2F(offset.x + mCellSize.y / 2, offset.y + mCellSize.y / 2), 5);
   }
}

bool GuiPopupMenuTextListCtrl::onKeyDown(const GuiEvent &event)
{
   //if the control is a dead end, don't process the input:
   if (!mVisible || !mActive || !mAwake)
      return false;

   //see if the key down is a <return> or not
   if (event.modifier == 0)
   {
      if (event.keyCode == KEY_RETURN)
      {
         mBackground->close();
         return true;
      }
      else if (event.keyCode == KEY_ESCAPE)
      {
         mSelectedCell.set(-1, -1);
         mBackground->close();
         return true;
      }
   }

   //otherwise, pass the event to it's parent
   return Parent::onKeyDown(event);
}

void GuiPopupMenuTextListCtrl::onMouseDown(const GuiEvent &event)
{
   if(mLastHighlightedMenuIdx != -1)
   {
      //See if we're trying to click on a submenu
      if(mList[mLastHighlightedMenuIdx].text[1] != 1)
      {
         //yep, so abort
         return;
      }
   }

   Parent::onMouseDown(event);
}

void GuiPopupMenuTextListCtrl::onMouseUp(const GuiEvent &event)
{
   if (mLastHighlightedMenuIdx != -1)
   {
      //See if we're trying to click on a submenu
      if (mList[mLastHighlightedMenuIdx].text[1] != 1)
      {
         //yep, so abort
         return;
      }
   }

   Parent::onMouseUp(event);

   S32 selectionIndex = getSelectedCell().y;

   if (selectionIndex != -1)
   {
      MenuItem *item = &mPopup->mMenuItems[selectionIndex];

      if (item)
      {
         if (item->mEnabled)
         {
            if(mMenuBar)
            {
               mMenuBar->closeMenu();
            }
            Con::executef(mPopup, "onSelectItem", Con::getIntArg(getSelectedCell().y), item->mText.isNotEmpty() ? item->mText : String(""));
         }
      }
   }

   mSelectedCell.set(-1, -1);
   mBackground->close();
}

void GuiPopupMenuTextListCtrl::onCellHighlighted(Point2I cell)
{
   // If this text list control is part of a submenu, then don't worry about
   // passing this along
   if (!isSubMenu)
   {
      RectI globalbounds(getBounds());
      Point2I globalpoint = localToGlobalCoord(globalbounds.point);
      globalbounds.point = globalpoint;
   }

   S32 selectionIndex = cell.y;

   if (selectionIndex != -1 && mLastHighlightedMenuIdx != selectionIndex)
   {
      mLastHighlightedMenuIdx = selectionIndex;

      mPopup->hidePopupSubmenus();
   }

   if (selectionIndex != -1)
   {
      MenuItem *list = &mPopup->mMenuItems[selectionIndex];

      if (list->mIsSubmenu && list->mSubMenu != nullptr)
      {
         list->mSubMenu->showPopup(getRoot(), getPosition().x + mCellSize.x, getPosition().y + (selectionIndex * mCellSize.y));
      }
   }
}
