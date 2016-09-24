﻿// ClassicalSharp copyright 2014-2016 UnknownShadow200 | Licensed under MIT
using System;
using System.Drawing;
using ClassicalSharp;
using Launcher.Gui.Widgets;
using OpenTK.Input;

namespace Launcher.Gui.Views {
	
	public abstract class IView {
		protected internal LauncherWindow game;
		protected internal IDrawer2D drawer;
		
		internal int widgetIndex;
		internal LauncherWidget[] widgets;
		protected Font titleFont, inputFont, inputHintFont;
		
		public IView( LauncherWindow game ) {
			this.game = game;
			drawer = game.Drawer;
		}
		
		/// <summary> Function called to setup the widgets and other data for this view. </summary>
		public abstract void Init();
		
		/// <summary> Function called to redraw all widgets in this view. </summary>
		public virtual void DrawAll() {
			MakeWidgets();
			RedrawAllButtonBackgrounds();
			
			using( drawer ) {
				drawer.SetBitmap( game.Framebuffer );
				RedrawAll();
			}
		}
		
		/// <summary> Cleans up all native resources held by this view. </summary>
		public virtual void Dispose() {
			if( titleFont != null ) titleFont.Dispose();
			if( inputFont != null ) inputFont.Dispose();
			if( inputHintFont != null ) inputHintFont.Dispose();
		}
		
		/// <summary> Creates or updates all the widgets for this view. </summary>
		protected abstract void MakeWidgets();
		
		protected void RedrawAllButtonBackgrounds() {
			int buttons = 0;
			for( int i = 0; i < widgets.Length; i++ ) {
				if( widgets[i] == null || !(widgets[i] is LauncherButtonWidget) ) continue;
				buttons++;
			}
			if( buttons == 0 ) return;
			
			using( FastBitmap bmp = game.LockBits() ) {
				for( int i = 0; i < widgets.Length; i++ ) {
					if( widgets[i] == null ) continue;
					LauncherButtonWidget button = widgets[i] as LauncherButtonWidget;
					if( button != null )
						button.RedrawBackground( bmp );
				}
			}
		}
		
		protected void RedrawAll() {
			for( int i = 0; i < widgets.Length; i++ ) {
				if( widgets[i] == null ) continue;
				widgets[i].Redraw( drawer );
			}
		}

		protected LauncherWidget MakeInput( string text, int width,
		                                   bool password, int maxChars, string hint ) {
			return Makers.Input( this, text, width, inputFont,
			                    inputHintFont, password, maxChars, hint );
		}
	}
}
